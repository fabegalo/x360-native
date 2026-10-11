# x360-native (nome provisório)

Plataforma para Windows x86-64, baseada em [ReXGlue](https://github.com/rexglue/rexglue-sdk),
cujo objetivo de longo prazo é executar jogos de Xbox 360 recompilados
estaticamente (AOT) para código nativo e executar a **dashboard original** do
console, com lançamento de jogos e retorno ao menu.

**Estado atual: fundação (M0).** O SDK é compilado a partir de uma revisão fixada
e sua suíte de testes roda; um programa PowerPC próprio passa por
montagem → recompilação → compilação → execução. **Nenhum jogo e nenhuma versão
da dashboard foram executados.** Ver [STATUS.md](STATUS.md).

Recompilar a CPU não elimina a emulação do resto do console: kernel, XAM,
sistema de arquivos, áudio e GPU Xenos continuam sendo reproduzidos pelo runtime.
Ver [docs/architecture.md](docs/architecture.md).

## Requisitos (verificados nesta máquina)

- Windows 10/11 x64
- Visual Studio 2022 Build Tools com a carga de trabalho C++ (MSVC x64, Windows SDK).
  Testado: 17.14.41, MSVC 14.44.35207, SDK 10.0.26100. CMake 3.31 e Ninja 1.12
  vêm junto com o VS.
- Git. Não precisa de Python nem de privilégios de administrador.
- LLVM/Clang **22.1.8**: baixado por `scripts/bootstrap-llvm.ps1` para
  `.local/toolchains` (verificação SHA-256; sem alterar PATH nem registro).
  O SDK recusa compiladores que não sejam Clang ≥ 18.
- ~8 GB livres (LLVM ~3,9 GB extraído; build Debug).

## Comandos

Executados nesta ordem no PowerShell, na raiz do repositório:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\bootstrap.ps1
scripts\devshell.cmd cmake --preset win-amd64
scripts\devshell.cmd cmake --build --preset win-amd64-debug
scripts\devshell.cmd ctest --preset win-amd64-debug -j 16
```

- `bootstrap.ps1`: submódulos do SDK, LLVM fixado, cópia verificada dos symlinks
  do libmspack (necessária quando a conta não pode criar symlinks).
- `devshell.cmd`: executa um comando com o ambiente do MSVC (`vcvars64`) e o LLVM
  fixado no PATH, só para aquele processo.
- O configure leva ~7 min na primeira vez (submódulos grandes, FFmpeg); o build
  Debug completo, ~3 min (medido: 166 s num Ryzen 7 5700X3D).
- A linha `'vswhere.exe' não é reconhecido` pode aparecer: vem do `VsDevCmd.bat`
  da Microsoft quando `NoDefaultCurrentDirectoryInExePath=1` e não afeta o ambiente.

Resultado esperado de `ctest` hoje: 1703 testes, 1697 aprovados, 5 ignorados,
1 falha conhecida do SDK (`unit.Depfile escapes characters make treats as syntax`,
bug no próprio teste; ver STATUS.md).

Subconjuntos:

```powershell
scripts\devshell.cmd ctest --preset win-amd64-debug -L project   # testes deste repositório
scripts\devshell.cmd ctest --preset win-amd64-debug -L ppc       # suíte PPC do SDK
scripts\devshell.cmd ctest --preset win-amd64-debug -R "local[.]"  # exige arquivos em .local/inputs
```

## Supervisor `x360n`

Ferramenta de desenvolvimento por terminal (não é a dashboard).
Executável: `.local\build\win-amd64\src\supervisor\Debug\x360n.exe`.

```powershell
x360n doctor                 # toolchain, SDK fixado, ferramentas do SDK
x360n identify <arquivo>     # SHA-256 e identidade XEX2 (--json disponível)
x360n run <exe> [--label L] [--timeout S] [-- args...]
```

`run` só aceita executáveis gerados pelo build (`.local/build` ou
`external/rexglue-sdk/out`) e nunca arquivos de `.local/inputs`. Cada execução
cria `.local/runs/<UTC>-<label>/` com `stdout.log`, `stderr.log` e `report.json`
(hash do executável, revisões, código de saída, duração, contagem de stubs).

Códigos de saída:

| Comando | 0 | 1 | 2 | 3 |
|---|---|---|---|---|
| `doctor` | sem falhas | alguma verificação falhou | — | uso inválido |
| `identify` | identificado (ou formato desconhecido) | erro de leitura ou XEX2 malformado | — | arquivo inexistente / uso |
| `run` | filho saiu com 0 | filho saiu ≠ 0 | timeout | recusado ou não iniciado |

## Arquivos locais

Tudo que é da máquina ou derivado de conteúdo do usuário fica em `.local/`
(ignorado pelo git): `toolchains/`, `build/`, `inputs/` (XEX, dashboard, jogos
fornecidos pelo usuário com autorização), `runs/`, `logs/`. Nada de conteúdo
proprietário entra no repositório.

## Escopo

Offline. Xbox Live, multiplayer online e periféricos especiais (Kinect etc.)
estão fora do escopo atual. Xenia é usado apenas como referência técnica.

## Licenças

ReXGlue: BSD-3-Clause (com partes derivadas do Xenia, BSD). Binutils PowerPC
incluídos no SDK: GPL-3.0 (usados como ferramenta, não ligados). LLVM: Apache-2.0
com exceção LLVM. A licença deste repositório ainda não foi definida.
