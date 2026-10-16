# Integração na main — 2026-10-15

Escopo autorizado: integrar todas as branches e alterações pendentes, validar
build/testes pertinentes, publicar `main` e excluir branches integradas. Sem
force-push, tags ou Release.

## Histórico e preservação

- Base remota: `c43d679`; Native/desktop: `8147359`; Xenos: `bb2b8e9`.
- Native/desktop integrado por fast-forward; Xenos por merge `0643c94`, sem
  conflitos textuais. Nenhum commit anterior foi reescrito.
- Worktrees de distribuição em `2b4b9b8` e `f6fee9e` já eram ancestrais da base.
  Não havia stashes ou alterações próprias não commitadas nos worktrees.
- SDK permanece em `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, com 76 patches
  ativos conferidos. As modificações locais reproduzíveis do SDK são preservadas,
  assim como os arquivos ignorados, originais, perfis, saves e worktrees.
- Bundle pré-integração verificado e diff binário do SDK em
  `.local/preserved/integration-20261015/`, fora da publicação.
- O remoto tinha somente `main`; não havia branches remotas extras para excluir.

## Validação e correções de integração

Configure e build Debug completos pelo `scripts/devshell.cmd`, com LLVM 22.1.8
e `X360N_NATIVE_RENDERER=ON`. O primeiro build detectou include RenderDoc ausente
nos dois novos probes Xenos; os diretórios privados foram declarados no CMake.
O build após a correção e o build final terminaram com saída 0.

A seleção de 22 testes abrange catálogo desktop, Native, os probes Xenos,
renderer global, handoff/sessão e isolamento de originais. A primeira execução
teve 20 aprovados, uma falha de ambiente (CMake fora do PATH no gate de assets)
e timeout de 30 s no teste Native em Debug. O gate passou pelo devshell.

A repetição Native isolada completou 366.351 asserções, mas uma falhou durante
a remoção da pasta temporária do cache; ela estava vazia após o processo.
A limpeza agora tolera brevemente handles de exclusão pendentes no Windows,
com limite de um segundo e asserção de sucesso, sem remoção recursiva. O limite
CTest do conjunto de texturas passou a 180 s para a comparação GPU/CPU em Debug;
nenhum caso, comparação ou asserção de conteúdo foi removido.

Resultado final: os 22 testes selecionados passaram, contando as duas repetições
focadas. `native.texture` passou pelo CTest em 71,52 s. Os probes Xenos passaram
204 verificações de resolve e 2.328 de vertex fetch. As falhas iniciais acima
permanecem registradas, sem repetir os 20 testes já aprovados.

Logs de configure, builds, tentativas e XML dos testes ficam no mesmo diretório
local de preservação. Nenhum jogo, teste visual, Computer Use ou suíte completa
foi executado. Evidências gráficas anteriores e seus limites continuam válidos;
esta integração não certifica desempenho, fidelidade integral ou novos títulos.
