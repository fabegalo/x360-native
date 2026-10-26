# Forza Horizon 2 e Minecraft — suporte genérico

Escopo: fazer os dois jogos locais funcionarem sem ajustes por título.
Preservar o SDK pinado, a semântica PPC/XAM, os originais e os dados de usuário.

## Preparação: bloqueio identificado em 25/10

Forza Horizon 2, Title ID `4D530AA4`, versão `0.0.0.12`, XEX SHA-256
`516b04d7c69313cc581062e44030af3636f689e7f09eed1a709e4319f031feb7`:
a geração falhava em `Unsupported AOT dependency identity: $systemupdate/avatareditor.xex`.
O gerador examinava aplicações e módulos do firmware contidos no disco como
dependências do jogo. A correção genérica `e407eb8` exclui somente a pasta
reservada `$SystemUpdate` na raiz da descoberta de módulos AOT, preservando
todos os arquivos e hashes em `assets.sha256`.

O teste sintético reproduziu a falha antes da correção. Depois, o filtro
`[generic-aot]` passou 114 verificações em quatro casos. Inclui DLLs de jogo,
basenames repetidos no firmware, payload de firmware não analisável e uma pasta
de mesmo nome dentro de `assets/`, que continua sujeita à análise normal.
O build foi feito via `scripts/devshell.cmd`, em Release. A publicação atômica
dos temporários falhou no sandbox; os testes foram repetidos fora dele.

A geração real do Forza passou 12 verificações e declarou somente as duas DLLs
do jogo: `SpeechFacade_default.xex` e `XMediaFacade_default.xex`. Não foram
acrescentados perfis manuais ou hints de funções.

## Minecraft local

O catálogo não continha Minecraft na retomada inicial. O pacote local
`584111F7/000D0000/49AAD81B9FCDA45E4A03D71BFCB353F8FADB236C58` foi reimportado
pelo fluxo existente de extração/importação e geração AOT. Versão `0.0.0.1`,
XEX SHA-256 `6d79bb823091890dabbe586e15c0744f707565c980ad998eaf43c808731308d1`.
Importação e geração passaram oito verificações. Trata-se do executável do
pacote do jogo; o bootstrap e o Title Update presentes no disco são distintos.

## Retomada de 26/10

A preparação anterior do Forza foi interrompida durante a compilação da
ferramenta SDK, sem recibo final. Foi retomada pelo driver oficial, com limite
de 40 minutos e acompanhamento. A geração de projeto não comprova gameplay.
O usuário autorizou testes visuais e Computer Use nestes dois jogos, com cópias
isoladas dos dados e limite de três minutos por tentativa.

Evidências privadas: `.local/evidence/generic-games-2026-10-25/` e os logs
`x360n-build-*` nas árvores `.local/build/aot/ga4cd45215ceb5054` e
`.local/build/aot/gf107b6bf6a6a3256`.
