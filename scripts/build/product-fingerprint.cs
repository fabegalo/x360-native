// Private Ninja checkpoint computation. No persistent hash/approval cache.
using System;
using System.IO;
using System.Text;
using System.Collections.Generic;
using System.Text.RegularExpressions;
using System.Security.Cryptography;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public sealed class X360NProduct {
    public string path { get; set; }
    public string signature { get; set; }
    public string sha256 { get; set; }
}
public sealed class X360NReceiptEntry {
    public string path { get; set; }
    public string sha256 { get; set; }
}
public sealed class X360NProducts : IDisposable {
    static readonly StringComparer Comparer = StringComparer.OrdinalIgnoreCase;
    readonly Dictionary<string, string> hashes = new Dictionary<string, string>(Comparer);
    // Do not assume case-insensitive directory entries for path protection.
    readonly Dictionary<string, string> paths = new Dictionary<string, string>(StringComparer.Ordinal);
    readonly HashSet<string> directories = new HashSet<string>(StringComparer.Ordinal);
    readonly List<SafeFileHandle> directoryReaders = new List<SafeFileHandle>();
    readonly List<FileStream> readers = new List<FileStream>();
    readonly byte[] buffer = new byte[1048576];
    readonly bool windows = Environment.OSVersion.Platform == PlatformID.Win32NT;
    // Framework exposes CNG through System.Core; newer .NET uses its native
    // SHA256 implementation. Avoid a compile-time dependency on the former.
    static readonly Type cng = Type.GetType("System.Security.Cryptography.SHA256Cng, System.Core", false);
    static string HashStream(Stream stream, byte[] buffer) {
        using (SHA256 sha = Environment.OSVersion.Platform == PlatformID.Win32NT && cng != null
            ? (SHA256)Activator.CreateInstance(cng) : SHA256.Create()) {
            int count;
            while ((count = stream.Read(buffer, 0, buffer.Length)) != 0) sha.TransformBlock(buffer, 0, count, null, 0);
            sha.TransformFinalBlock(buffer, 0, 0);
            return Hex(sha.Hash);
        }
    }
    public static X360NReceiptEntry[] ReceiptEntries(string[] files) {
        var result = new List<X360NReceiptEntry>(); var buffer = new byte[1048576];
        foreach (string file in files) {
            string path = Path.GetFullPath(file);
            using (var stream = File.OpenRead(Io(path))) result.Add(new X360NReceiptEntry { path = path, sha256 = HashStream(stream, buffer) });
        }
        return result.ToArray();
    }
    public static bool VerifyReceipt(string[] files, string[] expected) {
        if (files.Length != expected.Length) return false;
        var buffer = new byte[1048576];
        for (int i = 0; i < files.Length; ++i) {
            string path = Io(files[i]);
            if (!File.Exists(path)) return false;
            // No approval/hash cache here: read every file on every call,
            // holding its read handle against writes for that entire read.
            using (var stream = File.OpenRead(path)) if (!Comparer.Equals(HashStream(stream, buffer), expected[i])) return false;
        }
        return true;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct FileInfo {
        public uint Attributes, CreationLow, CreationHigh, AccessLow, AccessHigh, WriteLow, WriteHigh;
        public uint Volume, SizeHigh, SizeLow, Links, IndexHigh, IndexLow;
    }
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool GetFileInformationByHandle(SafeFileHandle file, out FileInfo info);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern SafeFileHandle CreateFile(string path, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
    bool PinDirectory(string path) {
        if (directories.Contains(path)) return true;
        string parent = Path.GetDirectoryName(path);
        if (!String.IsNullOrEmpty(parent) && !PinDirectory(parent)) return false;
        SafeFileHandle handle = CreateFile(Io(path), 0x80000000, 1, IntPtr.Zero, 3, 0x02200000, IntPtr.Zero);
        FileInfo info;
        if (handle.IsInvalid || !GetFileInformationByHandle(handle, out info) || (info.Attributes & 0x410) != 0x10) { handle.Dispose(); return false; }
        directoryReaders.Add(handle); directories.Add(path); return true;
    }
    static string Io(string path) {
        path = Path.GetFullPath(path);
        if (Environment.OSVersion.Platform != PlatformID.Win32NT || path.StartsWith(@"\\?\")) return path;
        return path.StartsWith(@"\\") ? @"\\?\UNC\" + path.Substring(2) : @"\\?\" + path;
    }
    static string Resolve(string build, string file) { return Path.GetFullPath(Path.IsPathRooted(file) ? file : Path.Combine(build, file)); }
    static string Unescape(string label) { return label.Replace(@"\\", @"\").Replace("\\\"", "\""); }
    public static string[] GraphNodes(string build, string graph) {
        var result = new List<string>();
        var pattern = new Regex(@"^""[^"" ]+"" \[label=""((?:\\.|[^""\\])*)""\]$");
        using (var input = new StringReader(graph)) {
            string line;
            while ((line = input.ReadLine()) != null) {
                var match = pattern.Match(line);
                if (match.Success) result.Add(Resolve(build, Unescape(match.Groups[1].Value)));
            }
        }
        return result.ToArray();
    }
    public static string[] CompilerDependencies(string build, string deps, string[] objects) {
        var wanted = new HashSet<string>(objects, Comparer);
        var result = new HashSet<string>(Comparer);
        var pattern = new Regex(@"^(.+): #deps ");
        using (var input = new StringReader(deps)) {
            string line; bool selected = false;
            while ((line = input.ReadLine()) != null) {
                var match = pattern.Match(line);
                if (match.Success) selected = wanted.Contains(Resolve(build, match.Groups[1].Value));
                else if (selected && line.StartsWith("    ")) result.Add(Resolve(build, line.Substring(4)));
            }
        }
        var sorted = new List<string>(result); sorted.Sort(StringComparer.CurrentCultureIgnoreCase);
        return sorted.ToArray();
    }
    static IEnumerable<string> Parents(Dictionary<string, List<string>> edges, string id) {
        List<string> result; return edges.TryGetValue(id, out result) ? result : (IEnumerable<string>)new string[0];
    }
    static string Hex(byte[] hash) { return BitConverter.ToString(hash).Replace("-", "").ToLowerInvariant(); }
    string Digest(string path) {
        string cached;
        if (paths.TryGetValue(path, out cached)) return cached;
        path = Path.GetFullPath(path);
        if (paths.TryGetValue(path, out cached)) return cached;
        if (!File.Exists(Io(path))) return "<missing>";
        bool pinned = windows && PinDirectory(Path.GetDirectoryName(path));
        FileStream stream = null;
        if (pinned) {
            // Open the leaf itself. A symlink is never entered in the path
            // cache: its directory entry could be replaced while its target
            // remains open. Ordinary leaves and all their ancestors are held.
            SafeFileHandle handle = CreateFile(Io(path), 0x80000000, 1, IntPtr.Zero, 3, 0x08200000, IntPtr.Zero);
            FileInfo info;
            if (handle.IsInvalid) {
                int error = Marshal.GetLastWin32Error(); handle.Dispose();
                if (error == 2 || error == 3) return "<missing>";
                throw new IOException("Cannot protect checkpoint input: " + path + " (" + error + ")");
            }
            if (!GetFileInformationByHandle(handle, out info)) { handle.Dispose(); throw new IOException("Cannot identify checkpoint input: " + path); }
            if ((info.Attributes & 0x400) != 0) { handle.Dispose(); pinned = false; }
            else { try { stream = new FileStream(handle, FileAccess.Read); } catch { handle.Dispose(); throw; } }
        }
        if (stream == null) {
            if (!File.Exists(Io(path))) return "<missing>";
            stream = new FileStream(Io(path), FileMode.Open, FileAccess.Read, FileShare.Read, 4096, FileOptions.SequentialScan);
        }
        try {
            string key = null;
            if (windows) {
                FileInfo info;
                if (!GetFileInformationByHandle(stream.SafeFileHandle, out info)) throw new IOException("Cannot identify checkpoint input: " + path);
                if (info.IndexHigh != 0 || info.IndexLow != 0) key = info.Volume + ":" + info.IndexHigh + ":" + info.IndexLow;
                if (key != null && hashes.TryGetValue(key, out cached)) { if (pinned) paths[path] = cached; return cached; }
            }
            string result = HashStream(stream, buffer);
            if (key != null) { hashes.Add(key, result); readers.Add(stream); stream = null; }
            if (pinned && key != null) paths[path] = result;
            return result;
        } finally { if (stream != null) stream.Dispose(); }
    }
    public void Dispose() {
        foreach (FileStream file in readers) file.Dispose();
        foreach (SafeFileHandle directory in directoryReaders) directory.Dispose();
    }
    public static X360NProduct[] Compute(string build, string graph, string deps, string[] outputs, string[] commands, string[] libraries, string[] resources) {
        using (X360NProducts batch = new X360NProducts()) { return batch.Collect(build, graph, deps, outputs, commands, libraries, resources); }
    }
    X360NProduct[] Collect(string build, string graph, string deps, string[] outputs, string[] commands, string[] libraries, string[] resources) {
        var nodes = new Dictionary<string, string>(); var rules = new HashSet<string>();
        var edges = new Dictionary<string, List<string>>();
        var nodePattern = new Regex(@"^""([^""]+)"" \[label=""((?:\\.|[^""\\])*)""(, shape=ellipse)?\]$");
        var edgePattern = new Regex(@"^""([^""]+)"" -> ""([^""]+)""(.*)$");
        using (var input = new StringReader(graph)) {
            string line;
            while ((line = input.ReadLine()) != null) {
                var match = nodePattern.Match(line);
                if (match.Success) {
                    if (match.Groups[3].Success) rules.Add(match.Groups[1].Value);
                    else nodes[match.Groups[1].Value] = Resolve(build, Unescape(match.Groups[2].Value));
                    continue;
                }
                match = edgePattern.Match(line);
                if (!match.Success || match.Groups[3].Value.Contains("style=dotted")) continue;
                List<string> parents;
                if (!edges.TryGetValue(match.Groups[2].Value, out parents)) edges[match.Groups[2].Value] = parents = new List<string>();
                parents.Add(match.Groups[1].Value);
            }
        }
        var dependencies = new Dictionary<string, HashSet<string>>(Comparer);
        var depPattern = new Regex(@"^(.+): #deps [0-9]+,.*\((VALID|STALE)\)$");
        using (var input = new StringReader(deps)) {
            string line; HashSet<string> selected = null;
            while ((line = input.ReadLine()) != null) {
                var match = depPattern.Match(line);
                if (match.Success) dependencies[Resolve(build, match.Groups[1].Value)] = selected = new HashSet<string>(Comparer);
                else if (selected != null && line.StartsWith("    ")) selected.Add(Resolve(build, line.Substring(4)));
            }
        }
        var commandMap = new Dictionary<string, string>(Comparer);
        for (int i = 0; i < commands.Length; ++i) if (!String.IsNullOrEmpty(commands[i])) commandMap[Resolve(build, outputs[i])] = commands[i];
        var ruleCommands = new Dictionary<string, string>();
        foreach (var node in nodes) {
            string command;
            if (commandMap.TryGetValue(node.Value, out command)) foreach (string parent in Parents(edges, node.Key)) if (rules.Contains(parent)) ruleCommands[parent] = command;
        }
        foreach (var node in nodes) foreach (string parent in Parents(edges, node.Key)) {
            string command; if (ruleCommands.TryGetValue(parent, out command)) commandMap[node.Value] = command;
        }
        var libraryParts = new List<string>(); var resourceParts = new List<string>();
        foreach (string path in libraries) if (!String.IsNullOrEmpty(path)) {
            string hash = Digest(path); if (hash == "<missing>") throw new IOException("Implicit library missing: " + path); libraryParts.Add(path + "=" + hash);
        }
        foreach (string path in resources) if (!String.IsNullOrEmpty(path)) {
            string hash = Digest(path); if (hash == "<missing>") throw new IOException("Resource missing: " + path); resourceParts.Add(path + "=" + hash);
        }
        var emitted = new HashSet<string>(Comparer); var products = new List<X360NProduct>();
        string boundary = Path.GetFullPath(build).TrimEnd('\\', '/') + Path.DirectorySeparatorChar;
        var manifestPattern = new Regex("/MANIFESTINPUT:(?:\"([^\"]+)\"|([^\\s\"]+))", RegexOptions.IgnoreCase);
        foreach (var node in nodes) {
            string path = node.Value, command;
            if (!commandMap.TryGetValue(path, out command) || !emitted.Add(path)) continue;
            if (Regex.IsMatch(command, @"(?:^|\s)codegen\s", RegexOptions.IgnoreCase) || path.EndsWith("codegen.build.stamp", StringComparison.OrdinalIgnoreCase)) continue;
            if (!path.StartsWith(boundary, StringComparison.OrdinalIgnoreCase)) throw new IOException("Build output outside its owner: " + path);
            var inputs = new HashSet<string>(Comparer);
            foreach (Match match in manifestPattern.Matches(command)) inputs.Add(Resolve(build, match.Groups[1].Success ? match.Groups[1].Value : match.Groups[2].Value));
            foreach (string parent in Parents(edges, node.Key)) {
                if (rules.Contains(parent)) { foreach (string id in Parents(edges, parent)) { string file; if (nodes.TryGetValue(id, out file)) inputs.Add(file); } }
                else { string file; if (nodes.TryGetValue(parent, out file)) inputs.Add(file); }
            }
            HashSet<string> headers;
            if (dependencies.TryGetValue(path, out headers)) inputs.UnionWith(headers);
            if (Regex.IsMatch(path, @"\.(obj|pch)$", RegexOptions.IgnoreCase) && File.Exists(Io(path)) && !dependencies.ContainsKey(path)) throw new IOException("Compiler dependency record missing: " + path);
            var parts = new List<string>(); parts.Add(command);
            if (Regex.IsMatch(path, @"\.(exe|dll|lib)$", RegexOptions.IgnoreCase)) parts.AddRange(libraryParts);
            if (path.EndsWith(".res", StringComparison.OrdinalIgnoreCase)) parts.AddRange(resourceParts);
            var sorted = new List<string>(inputs); sorted.Sort(StringComparer.CurrentCultureIgnoreCase);
            foreach (string file in sorted) parts.Add(file + "=" + Digest(file));
            string signature;
            using (SHA256 sha = SHA256.Create()) signature = Hex(sha.ComputeHash(Encoding.UTF8.GetBytes(String.Join("\n", parts))));
            products.Add(new X360NProduct { path = path, signature = signature, sha256 = Digest(path) });
        }
        return products.ToArray();
    }
}
