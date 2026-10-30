# Guide 17559 — transição original de fechamento

## Causa e mudança

O host destruía a aplicação ao receber `close_requested` e, na mesma passagem,
`HideSystemFrame` zerava a visibilidade, copiava o fade oculto para o alvo **e
para o valor atual** e publicava o descritor vazio. A composição desaparecia
sem executar o caminho de saída do HUD original.

Havia também uma pré-condição ausente: o controlador do fundo era construído
no estado 0, sem a chamada de abertura. O export original `XamShowGuideUI`
(`816C3580 -> 816C2AF0 -> 816CAC60`) seleciona modo **1**; o loop do host
passa esse modo a `81695868`. O host agora chama essa rotina ao abrir a Guide
como raiz, deixando o compositor executar a timeline original. Sem essa
inicialização, a rotina de fechamento apenas encontraria o fundo já oculto.
Restaurar uma Guide suspensa não reinicia a abertura do fundo.

O AOT da XAM Retail 17559 mostra a sequência em `816CE348..816CE3B0`:
alterar somente o alvo do fade, chamar `81695880(hud)` e continuar compondo
com `816CD6E8` enquanto o resultado for `8000000A`. `81695880` delega ao
controlador original do fundo (`81695218`), que inicia a timeline XUI em
`81693EF0` e consulta seu término. O compositor chama `816C7F20` para avançar
o fade atual. A publicação do descritor vazio acontece depois dessa espera.

`SystemUiCloseTransition` executa essa espera entre chamadas de `VdSwap`, sem
um laço bloqueante, duração inventada ou animação desenhada pelo host. Mantém
a consulta de disponibilidade do buffer (`81842110`) antes da composição
(`816CCF00`). O campo original host +220 continua controlando a alteração do
alvo; o host não escreve o fade atual no fechamento.

A aplicação deixa de receber updates após sua destruição. Sua DLL, pedido e
sessão de entrada permanecem vivos até o término da transição. Só então são
liberados e a notificação de UI oculta é emitida. As trocas para um sucessor
ou pai suspenso mantêm o comportamento anterior: carregamento na mesma
passagem, preservando composição e escurecimento, sem fechar o fundo.

O código é exclusivo do host 17559, incluindo sua DLL usada pelos jogos.
Não há alteração do SDK, renderer, módulos originais, perfis/saves ou hosts
1888/6717.

## Verificação

- Teste novo `[guide-transition]`: 181 asserções / um caso com cinco seções.
  Usa runtime e dispatcher reais com callbacks sintéticos, sem GPU. Verifica
  progresso em vários quadros, backpressure, preservação do fade atual,
  flag original +220, término imediato e erro original sem espera infinita;
  confere também a chamada de abertura com o modo original 1.
- `[profile-ui],[guide-context],[system-command-buffer]`: 311 asserções /
  15 casos aprovados. O comando incluiu também o filtro inexistente
  `[system-ui-input]`; os testes de entrada foram selecionados explicitamente
  na execução seguinte.
- `[guide-input],[guide-opening]`: 185 asserções / quatro casos aprovados.
- O primeiro build do teste novo detectou um include ausente de `xthread.h`;
  corrigido antes da execução dos testes. Não foram executados testes sobre
  o produto daquele build incompleto.
- Preparação oficial Windows x64/Release concluída, incluindo
  `dash_17559.exe` e `dash_17559_session_ui.dll`. A primeira rodada compilou,
  mas recusou o recibo porque o código de abertura foi ajustado durante sua
  execução. A rodada final, com código estabilizado, reutilizou o SDK,
  recompilou o produto e publicou o recibo novo em 30/10 às 00:47 (UTC−03).
  Verificados os 14 hashes dos artefatos do recibo e os hashes dos dois
  fontes da correção; marcador de preparação incompleta ausente. Evidência:
  `.local/evidence/validation/guide-close-20261030/products.json`.

São verificações do agendamento, ABI e ciclo de vida; os callbacks sintéticos
não validam os pixels ou a timeline original em execução. Na etapa automatizada
não houve dashboard, jogo ou sessão de Computer Use.

## Teste manual solicitado pelo mantenedor

Após o preparo, a dashboard foi aberta a pedido do mantenedor, com Xenos/D3D12,
dados de diagnóstico já isolados e supervisão limitada a 900 s por processo.
Não houve navegação automática pelo agente. O primeiro processo,
`20261030T035104Z-session-0-dash_17559`, terminou com código 0 em 125.497 ms,
sem timeout ou cancelamento, solicitando retorno à dashboard. Os logs registram
vários fechamentos seguidos por `original closing transition ended`, além da
troca Guide → Perfil → Guide sem ocultação intermediária do quadro de sistema.
Isso demonstra execução do caminho corrigido, sem estabelecer equivalência
visual quadro a quadro com o console. Não há medição nem alegação de ganho de
performance.
