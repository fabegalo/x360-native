# STATUS

Atualizado em 2026-10-11. Marcos: **M0 feito**; **M1 feito para um XEX** (amostra
XDK "Dolphin" da `rexglue/demo-iruka`), com as limitações abaixo.

## Funciona (com evidência)

| Item | Evidência |
|---|---|
| ReXGlue v0.10.0 (`f5337cdc`) compila no Windows com LLVM 22.1.8 + MSVC 14.44, modo submódulo | `cmake --build --preset win-amd64-debug` saída 0 |
| Suíte PPC do SDK (166 arquivos `.s`) | 1458/1458 casos |
| Testes unitários do SDK | 208 aprovados, 4 `SKIP` upstream, 1 falha (bug do teste, abaixo) |
| Programa PowerPC próprio pelo `recompile-tests` | `x360n.ppc.*` 4/4 |
| Supervisor `x360n doctor / identify / run` | testes `x360n.*`, `env.doctor`; `identify` classifica XEX2/XEX1/outra variante/inválido; `run` grava hashes do exe e das DLLs ao lado dele, diagnóstico do log e, com `--crash-stack`, a pilha de uma exceção não tratada |
| **Primeiro XEX real pelo fluxo completo** (identify → init → codegen → build → run) | `docs/validation-iruka.md`: `default.xex` `66ec34f2…` lido pelo carregador, 5222 funções geradas de novo pela v0.10.0 (determinístico), compilado, renderiza e anima por 30–40 s sem crash, 0 chamadas a stubs no log |
| Entradas locais | `local.inventory …` classifica os 621 XEX de `.local/inputs` (609 XEX2 lidos, 4 XEX1, 7 outras variantes, 1 inválido — listados em `.local/runs/inventory/inventory.tsv`); `local.integration targets …` confere os alvos de `tests/local/integration_targets.tsv` |
| **Segundo alvo: Castle Crashers (XBLA)** | `docs/validation-castle-crashers.md`: pacote LIVE extraído com `x360n-content` (708 arquivos), XEX criptografado + LZX lido pelo SDK, 3 bloqueios diagnosticados por desmontagem e corrigidos com ajustes de análise, menu principal renderizado; teclado e controle funcionando **segundo teste manual do usuário** |
| Reprodução do zero | árvore nova: configure 416 s, build Debug 166 s, testes 29 s |

Totais da última execução completa registrada (em d9b9c74; `ctest --preset win-amd64-debug -j 16`):
1703 testes, 1698 aprovados, 4 ignorados (upstream), 1 falha conhecida.

## Comandos executados (resumo)

```text
scripts\devshell.cmd cmake --preset win-amd64                       -> exit 0
scripts\devshell.cmd cmake --build --preset win-amd64-debug         -> exit 0
scripts\devshell.cmd ctest --preset win-amd64-debug -j 16           -> 1698/1703 (1 falha conhecida)
x360n identify .local\inputs\titles\iruka\extracted\default.xex      -> exit 0, XEX2 sem execution info
x360n run rexglued.exe -- init --project-name iruka ...              -> exit 0 (titles/iruka)
x360n run rexglued.exe -- codegen titles\iruka\iruka_manifest.toml   -> exit 0, 77 arquivos
scripts\devshell.cmd cmake -S titles/iruka -B .local/build/titles/iruka -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DREXSDK_DIR=... -> exit 0
scripts\devshell.cmd cmake --build .local/build/titles/iruka         -> 1ª falha (imgui.h), depois exit 0
x360n run .local\build\titles\iruka\iruka.exe --rex-log -- --gpu_plugin=xenos --game_data_root=... -> TIMEOUT (esperado), sem crash
```

Detalhes, mensagens exatas e classificação de cada falha: `docs/validation-iruka.md`.

## Falhas e desvios conhecidos

1. `unit.Depfile escapes characters make treats as syntax` (SDK) falha por escape
   inválido no próprio teste; `sdkcheck.*` comprova que a implementação está certa.
2. `rexglue --version` informa `0.10.0.0-dev.unknown` em modo submódulo.
3. Submódulo do SDK aparece modificado: 15 symlinks do libmspack materializados
   como cópias verificadas (a conta não cria symlinks).
4. Backport do upstream `b5e0cf8` (include de xxHash **e imgui**) em
   `cmake/x360n_sdk_backports.cmake`, usado pela árvore principal e pelos títulos;
   sem ele `unit_tests` e o `rex_app.cpp` de qualquer título não compilam.
5. `install` do SDK não executado (grava em `HKCU\Software\Kitware\CMake\Packages\rexglue`).
6. Cada título em modo `REXSDK_DIR` compila o SDK de novo na própria árvore
   (~7 min de configure + ~3 min de build a frio).
7. A demo `demo-iruka` **como publicada** não compila com a v0.10.0 (foi escrita
   para v0.1.0): formato de configuração, `find_package` de SDK instalado e API C++
   mudaram. É incompatibilidade de integração, não do XEX.
8. `.local/inputs/dashboard/Dashboards/` (1223 arquivos, adicionada pelo usuário)
   tem 11 XEX de pré-lançamento que `x360n identify` reconhece mas não lê — `XEX0`,
   `XEX?`, `XEX-` em `Blades/Beta/{0000,1529,1640}` e `XEX1` em `Blades/Beta/{1838,1839}`
   — e 1 arquivo inválido, `NXE/Beta/8955/dash.xex` (cabeçalho XEX2 sem o primeiro
   byte). Ficam no inventário como não suportados, com o motivo; nenhum foi alterado.

## Bloqueios

| Bloqueio | Impacto | O que destrava |
|---|---|---|
| Nenhum arquivo da dashboard | M2 não iniciado; experimento D1 parado | arquivos de uma versão em `.local/inputs/dashboard/<versão>/` |
| Sem symlinks no Windows | exige o passo de materialização | opcional: Developer Mode |

## Não funciona / não existe ainda

- Nenhuma dashboard. Castle Crashers sem save/load, sem versão completa (licença) e sem teste automatizado de gameplay.
- O `iruka` não foi validado em entrada, áudio, encerramento normal, execução longa nem Release.
- `x360n` não encadeia `init/codegen/build` sozinho: `scripts/build-title.ps1`
  faz o build em quatro fases, mas `init` e o codegen inicial ainda são passos à mão.
- Canal título→supervisor inexistente (os imports por ordinal são decodificados por
  `x360n-content imports` e classificados por `scripts/xex-imports.ps1`).

## Próximo incremento de maior valor

1. Castle Crashers: teste de save/load em `.local/user/castle_crashers`
   (gravar no jogo, encerrar, reabrir, conferir), que é a próxima etapa da matriz.
2. Transformar `identify → init → codegen → build-title → run` num comando do
   supervisor com relatório por etapa; os alvos já validados servem de regressão.

## Marcos

- **M0** build reproduzível, SDK integrado, testes reais — feito.
- **M1** um programa PowerPC autorizado em XEX pelo fluxo completo — feito para a amostra Dolphin (limitações acima).
- **M2** dashboard — não iniciado (sem arquivos).
- **M3 (parcial, experimental)** Castle Crashers: inicia, menu renderizado, entrada relatada pelo usuário; save/load e integração com a dashboard não testados.
- **M4–M5** — não iniciados.
