# Notificação original de conquista — 17559, 21/10/2026

## Evidência e causa

Na sessão do mantenedor `20261021T103941Z-session-1-forza_horizon`, a conquista
155 do título `4D5309C9` foi gravada às 10:48:47.9909152 UTC. O armazenamento
do perfil contém esse registro. Às 07:48:48.152 locais, a apresentação retornou
`8007000E`, depois da inicialização bem-sucedida do HUD original.

A sonda sem janela carregou somente o XAM UI original 17559 e seu módulo AOT,
sem executar o entrypoint da dashboard ou jogo. O construtor `816AB9C0`
retornou um apresentador válido. A fila `816ABFC0`, chamada fora de uma tarefa,
reproduziu exatamente `8007000E`; `816B8098` retornou zero.

O alocador do item da fila (`816AA070`) exige uma tarefa corrente, lê seus
atributos e fecha a referência temporária ao final. O host enviava a solicitação
no loop gráfico com contexto de processo de sistema, mas sem contexto de tarefa.
O erro não demonstra esgotamento da memória do computador.

## Correção

Patch reproduzível 0113 no SDK pinado: callbacks síncronos do host podem executar
como tarefas XAM com identidade e vida útil delimitadas. O contexto anterior é
restaurado também em chamadas aninhadas e exceções. Não é criada uma thread por
notificação. `XamTaskGetCurrentTask` retém a referência exigida pelo contrato;
`XamTaskGetAttributes` lê os dois atributos sob sincronização e rejeita argumentos
inválidos. Os handles do runtime e os handles originais não são intercambiados.

A ponte 17559 encaminha a identidade apenas durante o callback de sistema do host;
fora dele, preserva o caminho original, inclusive em workers comuns do SDK. Leitura
de atributos e fechamento distinguem handles do runtime dos originais. O envio executa
nesse contexto; o apresentador, a fila, as strings, a animação e a composição
continuam nos originais da 17559. Não há seleção por Title ID nem alterações no
armazenamento de conquistas, saves ou perfis do mantenedor.

## Validação técnica concluída

- Release: `[signin-task]`, 37 asserções em dois casos passaram, incluindo
  cancelamento/espera, referência adquirida/fechada, atributos, contextos aninhados,
  retorno de erro, handle encerrado, saída nula, restauração após exceção e
  preservação de workers comuns fora do contexto de sistema do host.
- Evidências locais: `.local/evidence/validation/notification-17559-20261021/`.
- A sonda compara o retorno sem contexto com o retorno sob o contexto corrigido.
  Seu armazenamento é temporário, separado dos perfis; não desbloqueia conquistas.
- A fila original retornou `8007000E` sem contexto e `0` dentro de
  `RunSystemTask`, na mesma execução isolada (`probe-queue-after.log`, saída 0).
- Repetido com a DLL final e verificação do item real: `probe-queue-final.log`,
  saída 0, `hosted_queue=0`, `queued_items=1`, `owns_text=1`. A fila mantém sua
  própria cópia UTF-16; o buffer temporário do emissor não fica referenciado.
- Os 112 patches ativos passaram na conferência de aplicação/hashes
  (`patch-check-final.log`). SDK continua no commit `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`.
- Builds finais Release de `dash_17559` e `forza_horizon` concluídos com saída 0
  e novos recibos de preparação. Marcadores de preparação incompleta ausentes.
- Sonda repetida com cópias isoladas dos runtimes dos dois produtos finais:
  `probe-product-dash.log` e `probe-product-forza.log`, ambas com saída 0,
  fila com um item e cópia própria do texto. As cópias testadas têm o mesmo hash
  das DLLs entregues com cada produto; nenhuma instalação manual foi feita.
- Hashes de executáveis, runtimes, módulo XAM UI, host de UI e recibos:
  `.local/evidence/validation/notification-17559-20261021/final-products.json`.
- O arquivo de desbloqueios do perfil manteve SHA-256
  `583aba07e3482aa9cec30b82506d8028b1ada0098f5c22cb0a6bee68d059e7a4`.
- Compilação e teste sem janela não certificam a exibição visual. Não foram
  repetidas suítes completas nem sessões visuais/controles já aprovados.

## Reteste do mantenedor: som sem imagem

Na sessão `20261021T114256Z-session-1-forza_horizon`, às 08:50:03.805,
o aviso entrou na fila sem erro. O log registra sucesso ao carregar `notify.xur`,
`notify.xus`, `Achievement.png` e `NotifyPopup.xma`. O mantenedor ouviu o som,
mas não viu o aviso. Portanto a correção da fila não bastou para o aceite.
A sessão posterior de Motocross `20261021T120832Z-session-1-motocross_madness`
não contém registro de envio de notificação; não é evidência de exibição.

O host parcial omitia o final da inicialização original `816CE8B0`:
`816CEBA8–816CEBB8` inicializa os campos +176 e +180 da estrutura em
`81AA20E0` com 1. A rotina original `816CE060` testa ambos e retorna antes
de chamar qualquer compositor quando um está zerado. A cena ainda pode
avançar e emitir som. O host agora inicializa esses campos uma vez, após
concluir o HUD/skin, preservando os demais bloqueios originais de desenho.
Não há alteração de Title ID, perfis ou recursos originais, nem custo novo
por frame. SDK e patch 0113 permanecem iguais.

Sonda local otimizada (`clang-cl /O2`, C++23): corpo AOT de `816CE060`
extraído sem alteração, com chamadas finais de desenho instrumentadas em
vez de GPU. Seis casos passaram: ambos zerados, cada campo isolado,
ambos habilitados sem HUD, com HUD e com bloqueio original ativo.
Artefatos `compositor-gate.cpp`, `.exe` e `.log` no diretório de evidências.
Isso comprova os desvios dessa rotina; **não testa pixels, animação ou GPU**.
Build de produto Release concluído com saída 0 pelo `scripts/build-title.ps1`,
registrado em `build-dash-visual.log` (run `0298d3b1910d4da3831544ee895228ca`).
Recibo novo, artefatos conferidos por SHA-256 e marcador de preparação incompleta
ausente. `visual-products.json` registra os hashes desta rodada e substitui os
hashes anteriores da 17559 para este reteste. O host compartilhado é o mesmo
carregado pelo launcher nas sessões de jogos; não houve cópia manual para jogos.

## Aceite visual após a correção de inicialização

O desbloqueio 155 existente deve ser preservado. Repeti-lo não deve somar pontos
nem gerar outra notificação. Obter outra conquista real ainda bloqueada, verificar
a notificação original, conferir nome/pontuação e incremento no Guide/perfil,
reiniciar e confirmar persistência. Não foi autorizada nem executada sessão de
gameplay pelo agente. Sem commit, push, tag ou Release antes da aprovação explícita.
