# Validação: rexglue/demo-iruka (amostra XDK "Dolphin")

Data: 2026-10-11. Ambiente: Windows 11 Pro 10.0.26300, Ryzen 7 5700X3D, RTX 4070,
LLVM/Clang 22.1.8 + MSVC 14.44. SDK: **ReXGlue v0.10.0, commit `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`**
(o `rexglue --version` imprime `0.10.0.0-dev.unknown`; ver STATUS.md, desvio 2).
Nenhum downgrade do SDK foi usado para os resultados abaixo.

## Regressão com o patch 0006

Regressão em 2026-10-12 com os patches 0001–0006: build do título e staging das
DLLs concluídos, sem regenerar código; `local.iruka.regression` passou nos cinco
níveis (inicia, carrega, executa sem falha, renderiza e anima), dentro da suíte
completa. Run `20261012T022946Z-regress-iruka`: 0 erros/stubs, 317 cores quantizadas,
diferença média entre capturas 7,016. DLLs e hashes estão no `report.json` do run.

## Entrada

| Item | Valor |
|---|---|
| Repositório da demo | `rexglue/demo-iruka@4d81580dd546d56e53250244b35f517b4d29e40a` (BSD-3-Clause), último commit "re-gen with rexglue v0.1.0" |
| `assets/assets.zip` | SHA-256 `719156a5a2bdef413f8ea5858560ba033c0bde320b7b0171656d6df07f50d2e0`, 1 293 751 bytes, 14 entradas (10 arquivos), nenhum caminho absoluto, com `..`, letra de unidade ou `\` |
| Caminho real do XEX | `default.xex` está na **raiz** do zip; extraído em `assets/` fica `assets/default.xex`, como o `iruka_config.toml` espera |
| `default.xex` | SHA-256 `66ec34f2f558fe4e419d926108bf458a5c634ea8b5f76771b48ba633ecb61e31`, 2 658 304 bytes |
| Recursos preservados | `Media/Fonts/Arial_16.xpr`, `Media/Meshes/{Dolphin1,2,3,SeaFloor}.xbg`, `Media/Resource.xpr`, `Media/Shaders/{DolphinTween,SeaFloor}.xvu`, `ShadeCausticsPixel.xpu` |
| Cópias | `.local/inputs/titles/iruka/extracted/` (nosso fluxo) e `assets/` da demo; hashes em `.local/inputs/titles/iruka/*.sha256`; após todas as execuções, idênticos e sem arquivos novos |

## 1. A demo como publicada, contra o SDK v0.10.0

A demo foi escrita para ReXGlue v0.1.0 (tag `74bcb7f4`, 2026-02-15). Resultado:
**não compila com v0.10.0**. Classificação: incompatibilidade da **camada de
integração** (CMake, formato de configuração, API C++). Não é falha do XEX.

| Etapa | Comando (na cópia `.local/reference/demo-iruka-copy`) | Resultado exato |
|---|---|---|
| Configuração CMake | `cmake -S <cópia> ...` | `CMake Error at CMakeLists.txt:15 (find_package): ... Could not find a package configuration file provided by "rexglue"` — a demo exige SDK instalado; não instalamos (o `install` grava em HKCU) |
| Configuração do codegen | `rexglue codegen <cópia>\iruka_config.toml` | exit 1: `iruka_config.toml is not a manifest: no [project] section. Pre-manifest single-file configs are no longer converted automatically; see docs for the layout.` |
| `rexglue migrate` | `rexglue migrate --app_root <cópia>` | exit 106: `A subcommand is required`. O comando existiu de v0.2.0 a v0.7.8 (`add2bdc`, 2026-02-23) e foi removido em `52e40ec` (2026-05-06, "replace migrate with codegen + upgrade detection flow"). |
| Compilação de `src/main.cpp` | `clang++ -fsyntax-only` com headers v0.10.0 e o C++ gerado pela v0.10.0 | `main.cpp:4:10: fatal error: 'generated/iruka_config.h' file not found` (a v0.10.0 gera só `iruka_init.h` e `iruka_pch.h`) |
| API usada pela demo, ausente na v0.10.0 | verificação de arquivos/macros/targets | `rex/kernel/xthread.h`, `rex/kernel/kernel_state.h`, `rex/runtime/guest/function.h` + `GUEST_FUNCTION_HOOK` (hoje `REX_HOOK` em `rex/hook.h`), target `rex::graphics`, `windowed_app_main_win.cpp` (hoje `windowed_app_main_sdl.cpp`) |

Foi iniciado um build do SDK v0.1.0 em `.local/reference/rexglue-sdk-v0.1.0`
para executar a demo na versão original; foi **interrompido** antes do fim, por
ser downgrade, e não produziu nenhum resultado usado aqui.

## 2. Integração atual (nosso fluxo, v0.10.0)

Projeto `titles/iruka/` criado por `rexglue init` (templates oficiais da revisão
fixada), compilado como projeto CMake próprio com `-DREXSDK_DIR=external/rexglue-sdk`
em `.local/build/titles/iruka` (Ninja, `RelWithDebInfo`, `-march=x86-64-v2`).
O C++ é gerado pelo `rexglue` compilado nessa mesma árvore, como etapa do build.

| # | Etapa | Resultado | Evidência / classificação |
|---|---|---|---|
| 1 | Identificação | ok | `x360n identify`: XEX2, flags `title`, **sem cabeçalho de execution info** (sem title id), PE `Dolphin.exe`, entry `0x82084E10`, base `0x82000000`, sem criptografia, compressão básica, imports `xam.xex` (16 entradas) e `xboxkrnl.exe` (203), XDK 2.0.7645.0 |
| 2 | Leitura pelo carregador do SDK | ok | codegen: `Title ID 00000000`, `Version 0.0.0.0`, `Filetime 2025-07-12`; runtime: `Function table initialized for module: code=82060000-8226A7CC, image=82000000-82290000`. Não houve rejeição. |
| 3 | Análise | ok, **com lacuna** | fases Register→Validate sem erro e sem `--force`. Sem os ajustes da demo, `0x82198CD8` (comparador do qsort, só alcançado por ponteiro) não é descoberto e surge uma entrada falsa `sub_82198D4C` no meio da função |
| 4 | Geração | ok | 77 arquivos, 5222 funções, 36 MB. Determinística: 0 diferenças entre a saída do `rexglued.exe` (Debug, árvore principal) e a do `rexgluerd.exe` (árvore do título) |
| 5 | Compilação/ligação | falha → ok | 1ª tentativa: `rex/ui/style.h:18:10: fatal error: 'imgui.h' file not found` ao compilar `rex_app.cpp` — bug do SDK v0.10.0 corrigido upstream em `b5e0cf8`; Pinyon Shift contorna igual (`PinyonShiftRexGlue.cmake:298-300`). Backport em `cmake/x360n_sdk_backports.cmake`, condicionado ao commit fixado. Depois: `iruka.exe` ligado |
| 6a | Execução (invocação errada) | falha | `[error] --game_data_root was not provided.` — a v0.10.0 só lê a pasta do jogo do cvar `--game_data_root` (`src/ui/rex_app.cpp:108-112`); o argumento posicional da wiki/README não existe mais. Abre também um MessageBox (`rex_app.cpp:204-208`). Classificação: nossa linha de comando |
| 6b | Execução **sem** ajustes da demo | falha | `0xC0000409` após 1,5 s; log: `[critical] [FATAL] Call to invalid or unregistered function at guest address 0x82198CD8`. Classificação: execução, por configuração de análise incompleta |
| 6c | Execução **com** os ajustes da demo | ok (limitado) | 2 execuções (40 s e 30 s; runs `20261011T070446Z` e `20261011T070909Z`) até o timeout do supervisor, sem crash; capturas de janela com golfinho, fundo do mar, texto "Dolphin" e o contador do próprio sample avançando (00m14s → 00m26s); log real (`--rex-log`): 0 linhas STUB, 0 erros, 2 avisos |

Ajustes de `iruka_config.toml` preservados em `titles/iruka/analysis/iruka.toml`,
incluídos por `[entrypoint].includes`:

| Ajuste | Ainda necessário? | Evidência |
|---|---|---|
| `0x82198CD8 = { name = "SortCombineUnrelatedCandidates" }` | **sim** | sem ele, falha 6b; com ele, a função é registrada (`SetFunction(0x82198CD8, ...)`) e `0x82198D4C` vira o rótulo interno `loc_82198D4C` |
| `setjmp_address = 0x82265050`, `longjmp_address = 0x82264D30` | aceito e aplicado | o código gerado passa a usar `ppc_longjmp`. Não há evidência de que o sample chame `longjmp` nesses 40 s; mantido por ser o valor da demo e válido na v0.10.0 |

Não portados: `src/main.cpp` da demo (o `ReXApp` da v0.10.0 já faz janela,
overlay F3, caminho do jogo e lançamento) e `src/hooks.cpp` (não era compilado
pela demo publicada; o equivalente atual seria `[rexcrt]`).

Linha de execução usada:

```powershell
x360n run .local\build\titles\iruka\iruka.exe --label iruka-B --timeout 30 --rex-log -- `
  --gpu_plugin=xenos --game_data_root=<raiz>\.local\inputs\titles\iruka\extracted `
  --user_data_root=<raiz>\.local\user\iruka-B
```

`--gpu_plugin=xenos` é necessário: o padrão do cvar é vazio, que desativa a
emulação de GPU (`rex_app.cpp:49-52`). O CMake do título passou a chamar
`rexglue_setup_target(iruka GPU_PLUGINS xenos)` para copiar o plugin.

## 3. Referência funcional e reprodução limpa

Níveis verificados automaticamente por `scripts/regress-iruka.ps1`
(CTest `local.iruka.regression`), separando "processo iniciou" de "funcionou":

| Nível | Critério | Resultado |
|---|---|---|
| L1 processo iniciou | `launched` no relatório | ok |
| L2 XEX carregado, guest iniciado | `Function table initialized for module` no log | ok |
| L3 executou sem falha | terminado pelo timeout do supervisor; 0 `[critical]`/`[error]`; 0 STUB | ok |
| L4 renderizou | captura com ≥ 32 cores quantizadas (calibração: quadro real 324, cor sólida 1) | ok (320–329) |
| L5 animou | duas capturas a ~6 s diferem ≥ 0,5 (calibração: mesma imagem 0,0) | ok (5,6–6,9) |

Reprodução em diretório de build limpo, incluindo a geração do código:

1. Primeira tentativa (configure + build numa árvore nova, sem `generated/default`):
   **falhou na ligação** com `lld-link: error: undefined symbol: struct rex::PPCImageInfo const PPCImageConfig`.
   Causa: `generated/rexglue.cmake` só inclui `generated/default/sources.cmake`
   se ele existir **no configure**; o codegen roda durante o build, e um novo
   `cmake --build` não reconfigura (testado: mesma falha). A wiki ("rebuild to
   pick up the generated sources") não basta nesta revisão.
2. Correção: configurar de novo depois do codegen. Codificado em
   `scripts/build-title.ps1` (configure → `<título>_codegen` → configure → build).
3. `scripts/build-title.ps1 -Title iruka -BuildDir .local/build/titles/iruka-clean-repro-2`
   numa árvore nova: exit 0 em 638 s (codegen 132 s, reconfigure 7 s, build 63 s),
   L1–L5 ok.
4. Duas gerações a partir do zero são **idênticas byte a byte** (0 de 80 arquivos).
   Uma geração incremental (sem → com ajustes) gera o **mesmo código por função**
   (5222/5222) e o mesmo conjunto de declarações; muda só a distribuição das
   funções entre arquivos (`codegen.partition.json`).

## Não validado

Entrada de controle, áudio, encerramento normal (o processo foi sempre terminado
pelo supervisor), execução longa, configuração Release, desempenho (o "fps" na
tela é o contador do próprio sample, não uma medição nossa) e qualquer outro XEX.
