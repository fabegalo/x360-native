# Validação experimental: Castle Crashers (XBLA)

Segundo alvo experimental, a partir de arquivos fornecidos pelo usuário em
`.local/inputs/titles/castle_crashers/source/` (não versionados; pacote realocado
da antiga pasta `Castle Crashers (World) (XBLA)`, com SHA-256 preservado).
Data: 2026-10-11. SDK: ReXGlue v0.10.0 `f5337cdc`; LLVM 22.1.8; build `RelWithDebInfo`.

## Entrada

| Item | Valor |
|---|---|
| Pacote | `584108B7/000D0000/F9432A7FE407A3C196C95D0FFD4A540937FD4EEC58`, 146 616 320 bytes, SHA-256 `ed7b65a8de5a70fabd311df4e542cf21adad9a75ac568b5f0a8c404c31d41e74` |
| Cabeçalho STFS (`x360n-content info`) | magic `LIVE`; content type `0x000D0000` (`kArcadeTitle`, `include/rex/system/xcontent.h:65`); title id `584108B7`; media id `1DF2F0B2`; versão `0x00000103`; volume type 0 (STFS); nome "Castle Crashers" |
| Extração (`x360n-content extract`) | 708 arquivos, 143 818 078 bytes, 0 erros, em `.local/inputs/titles/castle_crashers/extracted/`; manifesto SHA-256 em `metadata/extracted.sha256` (caminhos relativos a `extracted/`); o pacote original não é alterado |
| `default.xex` | 765 952 bytes, SHA-256 `825e9c8cb951d62ce954901488c5b783349c98cc5c03bd08ea0739df3af5d9ad`; único executável do pacote (nenhum outro `.xex`/`.dll`) |

`x360n-content` usa o `StfsContainerDevice` do SDK. A rotina de dump do próprio
SDK (`src/filesystem/vfs_dump.cpp`) não é compilada como ferramenta e grava
`base_path / entry->path()` sem validar os nomes vindos do pacote; a nossa
valida cada componente, recusa sobrescrever e grava o manifesto fora da árvore.

## Etapas

| # | Etapa | Resultado | Evidência / classificação |
|---|---|---|---|
| 1 | Identificação | ok | XEX2 `title`; title `584108B7`, media `1DF2F0B2`, versão `0.0.1.3` (coerente com o STFS `0x103`); PE `default.exe`; entry `0x820B7090`; base `0x82000000`; **criptografia normal, compressão normal (LZX)**; imports `xam.xex` 146 entradas, `xboxkrnl.exe` 255; XDK 2.0.7645.0 |
| 2 | Leitura pelo carregador do SDK | ok | `Title ID 584108B7`, `Version 0.0.1.3`, `Filetime 2008-07-23 08:54:20 UTC`; imagem carregada de 0x2D0000 bytes, SHA-256 `09539cbc…b710ba` (dois dumps idênticos). Exercita AES + LZX, que o `iruka` não exercita |
| 3 | Análise sem ajustes | **falha (1º bloqueio real)** | `UnresolvedCall 0x820E15E8 from 0x820E0D04: b 0x820E15E8 - target not in any function`; sem `--force`, exit 1 |
| 3' | Diagnóstico | causa identificada | desmontagem da imagem carregada: `0x820E0D00` é um *adjustor thunk* (`addi r3,r3,-4; b 0x820e15e8`); `0x820E15E8` é uma função completa entre palavras de preenchimento (`lis`×3, `addi`, `stw` em `r3`/`r3+4`, `blr` em `0x820E1610`), alcançada só pelo `b` do thunk |
| 4 | Análise + geração com ajuste | ok | `titles/castle_crashers/analysis/castle_crashers.toml`: `0x820E15E8 = {}` (só a entrada, sem nome nem tamanho). Codegen sem `--force`: exit 0, 41 arquivos, 3714 funções, 18 MB; `sub_820E15E8` termina no `blr` previsto e o thunk passa a chamá-la |
| 5 | Compilação (1ª) | **falha** | `castle_crashers_recomp.14.cpp: error: use of undeclared label 'loc_82112F90'` (e `F78`, `F70`) |
| 5' | Diagnóstico | causa identificada | `sub_82112BC0` foi estendida sobre a função seguinte `sub_82112D10` (sem palavra de preenchimento entre elas). Causa: tabela de saltos computada **sem verificação de limites** (`lbzx` em `0x8200AF18`, base `0x82112C08`, `bctr` em `0x82112C04`); a detecção automática leu 39 entradas, além da tabela, e alvos dentro de `sub_82112D10` estenderam o fim da função (`function_scanner.cpp:1972-1976`). `.pdata` tem o valor certo (0x150), mas `size` em `[functions]` não limita isso: é só dica para `discoverBlocks` (`phase_discover.cpp:97-99`); `size = 0x150` deixou a saída idêntica |
| 6 | Compilação com tabela manual | ok | `[[switch_tables]]` com os 16 bytes até a próxima tabela; o caso 4 (`0x42` → `0x82112D10`) sai como chamada de cauda; `check-pdata-overlap.ps1`: 0 de 2484; `castle_crashers.exe` ligado (`RelWithDebInfo`) |
| 7 | Execução 1 | **falha** | após ~4 s: `[FATAL] Call to invalid or unregistered function at guest address 0x82114E20` — função folha sem `.pdata`, alcançada por ponteiro (`mr r3,r4; lis/ori r4,0x4E574D20; li r5,0; b 0x8214aca4`). Também listada por `check-code-gaps.ps1` |
| 8 | Execução 2 (`0x82114E20 = {}`) | ok | exe SHA-256 `d6d79471…80e4466`; 60 s até o timeout do supervisor; 0 STUB, 0 mensagens não implementadas, 0 críticas. Capturas: tela "Press Start" (20 s) e **menu principal** (40 s): Local Game, Xbox LIVE Game, Player Statistics, Leaderboards, Achievements, Help & Options, **Unlock Full Game**, Return to Arcade |
| 9 | Teste manual do usuário | ok (relato) | o usuário informou em 2026-10-11 que o jogo **funciona com teclado e controle**. Não observado por nós; build e opções usadas por ele não registradas |

Comando da execução 2:

```powershell
x360n run .local\build\titles\castle_crashers\castle_crashers.exe --label cc-run-2 --timeout 60 --rex-log -- `
  --gpu_plugin=xenos --game_data_root=<raiz>\.local\inputs\titles\castle_crashers\extracted `
  --user_data_root=<raiz>\.local\user\castle_crashers
```

O teclado só emula o controle com `--mnk_mode=true` (padrão `false`,
`src/input/mnk/mnk_input_driver.cpp:26`; A = `Space`/`;`, Start = `Return`/`X`).
Uma execução 3 com teclas enviadas à janela (`scripts/window-route.ps1`) foi
interrompida pelo usuário antes de o supervisor gravar o relatório; não conta
como evidência.

## Observações

- **Modo trial provável**: o menu oferece "Unlock Full Game". `XamContentGetLicenseMask`
  devolve o cvar `license_mask`, padrão 0 (`src/kernel/xam/xam_content.cpp:24, 32-36`).
  Não alteramos a licença; a relação entre as duas coisas é hipótese.
- **Arquivos ausentes**: o jogo pede `game:\shaders\SobelEdge.xpu` e
  `game:\shaders\VertexColorFillPS.xpu` (`0xC000000F`); o pacote tem
  `SobelEdgePS.xpu` e nenhum dos dois. Não foi fatal. Se o console original faz o
  mesmo não foi verificado.
- **Lacunas de cobertura**: `check-code-gaps.ps1` lista 81 candidatos no
  Castle Crashers; no `iruka`, que funciona, lista 28. Um candidato sozinho não é
  defeito: só corrigimos endereços com falha observada e desmontagem conferida.

## Regressão com os patches locais do SDK (2026-10-11)

Depois dos patches 0001/0002 (`patches/rexglue-sdk/`), o título foi recompilado e
a execução 2 repetida nas mesmas condições (run `20261011T225432Z-cc-run-patched`,
sem entrada): 60 s até o timeout do supervisor, 0 STUB, 0 erros, 0 críticas, menu
principal na captura de 40 s. O log mostra a tabela de funções no mesmo heap de
antes: `Function table at 822D0000-824E5C27 in XEX heap 80000000 (10000-byte pages)`.

Depois dos patches 0003–0005 (2026-10-12): o codegen corrigido, rodado com
`--ignore-stamp`, gera saída idêntica (`41 unchanged`); o título foi recompilado (o
`rex_app.cpp` do 0004 entra no executável) e a execução 2 repetida (run
`20261012T012535Z-cc-run-patched-0005`, sem entrada): 60 s até o timeout, 0 STUB, 0
erros, 0 críticas, menu principal na captura de 40 s.

## Regressão com o patch 0006

Regressão em 2026-10-12 com 0001–0006: título recompilado e DLLs copiadas pelo
staging; codegen sem alterações. Run `20261012T023116Z-cc-regression-0006`: 90 s
até timeout, sem crash, 0 stubs, 0 erros e 0 críticas. Menu principal observado;
captura em `.local/logs/cc-regression-0006-menu.png`. Permanecem as duas aberturas
de shaders ausentes já conhecidas. Não houve nova validação de entrada/gameplay.

## Não validado

Gameplay além do relato do usuário, save/load, encerramento normal, sessão longa,
multijogador local, Xbox Live (fora do escopo), versão completa (licença) e
desempenho.

## Ferramentas criadas para este alvo

| Ferramenta | Uso |
|---|---|
| `x360n-content info/extract/dump-image` | cabeçalho STFS, extração validada com manifesto, imagem carregada (como o codegen) |
| `scripts/check-pdata-overlap.ps1` | funções geradas que passam do fim declarado no `.pdata` (iruka: 0/3725; Castle Crashers: 1/2484 antes da correção, 0 depois) |
| `scripts/check-code-gaps.ps1` | trechos de código sem função gerada (heurística; controle positivo: acha `0x82198CD8` no iruka sem ajustes) |
| `scripts/window-route.ps1` | teclas e capturas na janela de um processo lançado por nós |
