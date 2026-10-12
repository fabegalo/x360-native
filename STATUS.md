# STATUS

Atualizado em 2026-10-12. Marcos: **M0 feito**; **M1 feito para um XEX** (amostra
XDK "Dolphin" da `rexglue/demo-iruka`), com as limitações abaixo.

## Funciona (com evidência)

| Item | Evidência |
|---|---|
| ReXGlue v0.10.0 (`f5337cdc`) compila no Windows com LLVM 22.1.8 + MSVC 14.44, modo submódulo, com 5 patches locais | `cmake --build --preset win-amd64-debug` saída 0; patches em `patches/rexglue-sdk/`, verificados no configure e em todo build |
| Suíte PPC do SDK (166 arquivos `.s`) | 1458/1458 casos |
| Testes unitários do SDK | 208 aprovados, 4 `SKIP` upstream, 1 falha (bug do teste, abaixo) |
| Patches locais do SDK: 0001 tabela de funções no heap XEX certo; 0002 falha de setup sem desmontagem prematura; 0003 import com slot e thunk em registros diferentes; 0004 encerramento com a janela ainda viva; 0005 bit 0 de `KeGetImagePageTableEntry` | `sdkcheck.*` (11 casos): cada patch revertido sozinho faz falhar os seus (`patches/rexglue-sdk/README.md`, tabela antes/depois); o 0004 e o caminho do 0002 testados num `ReXApp` real (`tests/shutdown_probe`) |
| Programa PowerPC próprio pelo `recompile-tests` | `x360n.ppc.*` 4/4 |
| Supervisor `x360n doctor / identify / run` | testes `x360n.*`, `env.doctor`; `identify` classifica XEX2/XEX1/outra variante/inválido; `run` grava hashes do exe e das DLLs ao lado dele, diagnóstico do log e, com `--crash-stack`, a pilha de uma exceção não tratada |
| **Primeiro XEX real pelo fluxo completo** (identify → init → codegen → build → run) | `docs/validation-iruka.md`: `default.xex` `66ec34f2…` lido pelo carregador, 5222 funções geradas de novo pela v0.10.0 (determinístico), compilado, renderiza e anima por 30–40 s sem crash, 0 chamadas a stubs no log |
| Entradas locais | `local.inventory …` classifica os 621 XEX de `.local/inputs` (609 XEX2 lidos, 4 XEX1, 7 outras variantes, 1 inválido — listados em `.local/runs/inventory/inventory.tsv`); `local.integration targets …` confere os 4 alvos de `tests/local/integration_targets.tsv` |
| **Segundo alvo: Castle Crashers (XBLA)** | `docs/validation-castle-crashers.md`: pacote LIVE extraído com `x360n-content` (708 arquivos), XEX criptografado + LZX lido pelo SDK, 3 bloqueios diagnosticados por desmontagem e corrigidos com ajustes de análise, menu principal renderizado; teclado e controle funcionando **segundo teste manual do usuário**; mesmo resultado com o runtime corrigido (run `cc-run-patched`) |
| Regressão da referência | `local.iruka.regression` (L1–L5) passa dentro da suíte completa, com o runtime corrigido |
| **Dashboard 17559: o código executa** | `docs/dashboard.md` §0.8: codegen sem `--force`, 39 141 funções (30 entradas conferidas uma a uma, 1 tabela de saltos, 515 inícios só por ponteiro classificados por `scripts/check-pointer-targets.ps1`), todas registradas; `xstart` executa e a inicialização chega a áudio, idioma, fontes, perfil e à abertura de `shrdres.xzp`; para ~2 s depois numa página de sistema fixa (bloqueio abaixo). **Nenhum quadro apresentado**; menu e navegação não observados |
| Reprodução do zero | árvore nova: configure 416 s, build Debug 166 s, testes 29 s (antes dos patches; não refeito) |

Totais da última execução completa (`ctest --preset win-amd64-debug -j 16`, 2026-10-11,
com os patches 0001–0005, 2026-10-12): **1722 testes, 1717 aprovados, 4 ignorados (upstream), 1 falha** —
`unit.Depfile escapes characters make treats as syntax`, a falha conhecida (item 1).
`local.iruka.regression` passou.

## Comandos executados (resumo)

```text
scripts\devshell.cmd cmake --preset win-amd64                       -> exit 0
scripts\devshell.cmd cmake --build --preset win-amd64-debug         -> exit 0
scripts\devshell.cmd ctest --preset win-amd64-debug -j 16           -> 1717/1722 (4 skip, 1 falha conhecida)
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
   Continua falhando e é contada como falha, não como sucesso.
2. `rexglue --version` informa `0.10.0.0-dev.unknown` em modo submódulo.
3. Submódulo do SDK aparece modificado: 15 symlinks do libmspack materializados
   como cópias verificadas (a conta não cria symlinks) e os 5 patches locais
   (9 arquivos, listados em `patches/rexglue-sdk/README.md`), reaplicáveis por
   `scripts/apply-sdk-patches.ps1` e registrados com hashes em `deps.lock.json`.
4. Backport do upstream `b5e0cf8` (include de xxHash **e imgui**) em
   `cmake/x360n_sdk_backports.cmake`, usado pela árvore principal e pelos títulos;
   sem ele `unit_tests` e o `rex_app.cpp` de qualquer título não compilam.
5. `install` do SDK não executado (grava em `HKCU\Software\Kitware\CMake\Packages\rexglue`).
6. Cada título em modo `REXSDK_DIR` compila o SDK de novo na própria árvore
   (~7 min de configure + ~3 min de build a frio). As três árvores escrevem a mesma
   DLL em `external/rexglue-sdk/out/win-amd64`; cada título copia a sua no build
   (`x360n_stage_runtime_dlls`), e o `x360n run` registra o hash da que foi usada.
7. A demo `demo-iruka` **como publicada** não compila com a v0.10.0 (foi escrita
   para v0.1.0): formato de configuração, `find_package` de SDK instalado e API C++
   mudaram. É incompatibilidade de integração, não do XEX.
8. `.local/inputs/dashboard/Dashboards/` (1223 arquivos, adicionada pelo usuário)
   tem 11 XEX de pré-lançamento que `x360n identify` reconhece mas não lê — `XEX0`,
   `XEX?`, `XEX-` em `Blades/Beta/{0000,1529,1640}` e `XEX1` em `Blades/Beta/{1838,1839}`
   — e 1 arquivo inválido, `NXE/Beta/8955/dash.xex` (cabeçalho XEX2 sem o primeiro
   byte). Ficam no inventário como não suportados, com o motivo; nenhum foi alterado.
9. Imports `XUsbcam*` sem símbolo no runtime (`src/kernel/CMakeLists.txt:53`) e
   ordinais fora da tabela sem definição gerada: títulos que os usam não linkam
   sem definições próprias (`docs/architecture.md` §7, itens 13–14).

## Bloqueios

| Bloqueio | Impacto | O que destrava |
|---|---|---|
| **Dashboard 17559: página de sistema em endereço fixo** — a dashboard lê por endereço absoluto `0x8E038614`, `0x8E038630` (bit `0x40`) e `0x8E038678` (bit `0x2`); nada está mapeado ali: `Unhandled guest access violation: read of guest 0x8E038630` em `sub_92185790` ← `xstart` (`docs/dashboard.md` §0.8) | a inicialização para ~2 s depois do início, antes de qualquer quadro | **informação externa**: o conteúdo dessa página como o sistema do console a preenche (componente não determinado; não é arquivo do pacote), ou a decisão de aceitar uma página zerada como suposição registrada (é o que o Xenia faz, com "Data is currently unknown") |
| Dependências demonstradas e não demonstradas | demonstrada: `shrdres.xzp` (aberto por `media:` → `\SystemRoot`, já no pacote, montado só leitura). Abertos sem dispositivo: `DASHUSER:\ThematicSkin`, `DASHUSER:\VisionEffect` (não fatal até aqui). Não demonstradas: `dashnui.xex`, `SegoeXbox-Light.xtt`, `xam.xex` real | seguir a execução depois do bloqueio acima |
| Sem symlinks no Windows | exige o passo de materialização | opcional: Developer Mode |

## Não funciona / não existe ainda

- Dashboard: o código executa por ~2 s e para na página de sistema fixa (§ Bloqueios); nenhum quadro, menu ou navegação. 28 stubs diferentes já são chamados nesse trecho, e um stub não define r3. Castle Crashers sem save/load, sem versão completa (licença) e sem teste automatizado de gameplay.
- O `iruka` não foi validado em entrada, áudio, encerramento normal, execução longa nem Release.
- `x360n` não encadeia `init/codegen/build` sozinho: `scripts/build-title.ps1`
  faz o build em quatro fases, mas `init` e o codegen inicial ainda são passos à mão.
- Canal título→supervisor inexistente.

## Próximo incremento de maior valor

1. Dashboard 17559: decidir a página de sistema `0x8E038600` (conteúdo real ou
   página zerada como suposição registrada) e seguir a inicialização até o primeiro
   quadro, registrando aberturas, carregamentos e os stubs que mudam o caminho.
2. Castle Crashers: teste de save/load em `.local/user/castle_crashers`
   (gravar no jogo, encerrar, reabrir, conferir), que é a próxima etapa da matriz.
3. Transformar `identify → init → codegen → build-title → run` num comando do
   supervisor com relatório por etapa; os alvos já validados servem de regressão.

## Marcos

- **M0** build reproduzível, SDK integrado, testes reais — feito.
- **M1** um programa PowerPC autorizado em XEX pelo fluxo completo — feito para a amostra Dolphin (limitações acima).
- **M2** dashboard — versão 17559: análise, geração, linkedição, setup, carga e entrada no código feitos; a inicialização executa ~2 s e para numa página de sistema de conteúdo desconhecido; primeira tela não alcançada.
- **M3 (parcial, experimental)** Castle Crashers: inicia, menu renderizado, entrada relatada pelo usuário; save/load e integração com a dashboard não testados.
- **M4–M5** — não iniciados.
