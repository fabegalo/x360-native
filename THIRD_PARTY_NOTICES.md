# Licenças e atribuições de terceiros

O x360-native é independente e não possui afiliação oficial com a Microsoft.
A licença BSD-3-Clause do código próprio está em **LICENSE**. Ela não substitui
os termos dos componentes abaixo nem concede direitos sobre jogos ou software Xbox.

Os textos originais estão na pasta **licenses**, ao lado deste documento no ZIP.
O inventário verificável **licenses/inventory.json** registra origem, revisão,
forma de uso, texto e SHA-256. Os índices existentes `package.sha256` e
`app/workspace/payload.sha256` cobrem esses arquivos junto com o restante do pacote.

| Componente incorporado ao ZIP desktop | Forma | Termos originais |
|---|---|---|
| ReXGlue / partes derivadas de Xenia | Leitores ISO/STFS e utilitários compilados no extrator; headers e patches no payload | [BSD-3-Clause](licenses/rexglue-sdk.txt), [atribuições por arquivo](licenses/rexglue-attributions.txt) |
| fmt | Biblioteca estática | [MIT](licenses/fmt.txt) |
| CLI11 | Headers compilados | [BSD-3-Clause](licenses/cli11.txt) |
| toml++ | Headers compilados | [MIT](licenses/tomlplusplus.txt) |
| nlohmann/json 3.11.3, vendorizado por inja | Headers compilados no supervisor; copyright 2013–2023 Niels Lohmann no header | [MIT original vendorizada](licenses/nlohmann-json.txt) |
| spdlog | Biblioteca estática | [MIT](licenses/spdlog.txt); fmt externo listado acima |
| SIMDe | Headers usados pelo extrator | [MIT](licenses/simde.txt) |
| utfcpp | Headers usados pelo extrator | [BSL-1.0](licenses/utfcpp.txt) |
| Microsoft C++ Standard Library | Templates/objetos incorporados | [Apache-2.0 WITH LLVM-exception](licenses/microsoft-stl.txt), copyright Microsoft Corporation |
| CRT/UCRT Microsoft | Runtime estático Release | Termos de redistribuição Visual Studio/Windows SDK; não é coberto pela licença própria |
| SDL | Somente trechos de patches; não há SDL.dll no ZIP desktop | [Zlib](licenses/sdl3.txt) |

Preservar os copyrights, condições e disclaimers nos formatos exigidos pelos
textos originais. Patches em `app/workspace/patches/rexglue-sdk` identificam
modificações locais; seus hashes e a base estão no lock e em `package.json`.
O código adaptado em `src/content/xex_patch.cpp` deriva do ReXGlue/Xenia e conserva
esses avisos; não é compilado nos três executáveis deste ZIP.

O pacote contém `x360-native.exe`, `app/x360n.exe` e `app/x360n-extract.exe`.
Não contém jogos, dashboards, fontes Xbox, firmware, chaves, saída AOT, runtime
ReXGlue completo, FFmpeg, disasm GNU, SDL binário nem ferramentas de compilação.
As DLLs de sistema são fornecidas pelo Windows, não copiadas da máquina de build.

## Preparação opcional, mediante download autorizado

O assistente obtém SDK e ferramentas diretamente de suas origens fixadas e mantém
os arquivos legais dos arquivos baixados. Esses componentes têm termos próprios:

- LLVM/Clang: [Apache-2.0 WITH LLVM-exception e avisos adicionais](licenses/llvm.txt).
- CMake: [BSD-3-Clause e terceiros incluídos](licenses/cmake.txt).
- Ninja: [Apache-2.0](licenses/ninja.txt).
- MinGit/Git for Windows: [GPL-2.0-only para Git](licenses/mingit.txt), além das
  licenças transitivas em `usr/share/licenses`, `ucrt64/share/licenses` e `ucrt64/doc`.
- SDK completo: SDL Zlib, FFmpeg LGPL-2.1-or-later no build atual, libmspack
  LGPL-2.1-only, além das bibliotecas discriminadas no inventário.
- O decoder GNU `disasm` é GPL-2.0-or-later e é **vinculado** às ferramentas
  `rexglue`/`x360n-content`. Já binutils PowerPC é executado como ferramenta
  separada; isso não atribui automaticamente GPL ao aplicativo gráfico.

Redistribuir ferramentas GPL ou programas que as incorporam exige cumprir seus
termos, incluindo código-fonte correspondente. Redistribuir o runtime que
incorpora LGPL estaticamente exige uma via adequada de modificação/relinkedição,
fontes, avisos e permissões previstas nessa licença. Copiar apenas este documento
não atende essas obrigações. O ZIP desktop não inclui esses binários.

O inventário registra também bibliotecas opcionais/testes que não entram no ZIP.
Não se autoriza aqui redistribuição dos arquivos fornecidos pelo usuário.
Veja `app/workspace/docs/LICENSE_AUDIT.md` (ou `docs/LICENSE_AUDIT.md` no repositório)
para evidências, pendências e limites da auditoria. A publicação segue bloqueada
até resolver as pendências ali indicadas.
