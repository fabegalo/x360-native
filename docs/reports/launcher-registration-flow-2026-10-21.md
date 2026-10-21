# Fluxo do launcher e registro na My Games — 21/10/2026

## Problema e correção

Um jogo importado com build válido podia aparecer como “Pronto para a My Games”
apenas pelo recibo de preparação, mesmo sem registro no armazenamento da
dashboard. “Conferir e iniciar” chegava ao supervisor, que recusava corretamente
a sessão: `Prepare para registrar o conteúdo verificado na My Games: forza_horizon`.
A conferência do jogo apenas mudava seu estado, sem concluir o registro.

O fluxo compartilhado `CompleteLibraryPreparation` valida o build selecionado e
suas dependências e conclui os registros pendentes. Preparar reaproveita builds
válidos; iniciar conclui o registro antes do preflight independente do supervisor.
Recibos consultados apenas por metadados deixam de produzir o estado “Pronto”.
O registro usa os mecanismos existentes de cópia verificada, publicação do header
por último, cancelamento e preservação de arquivos divergentes. Não há caminhos
locais nem Title IDs especiais na correção. Dashboards sem opção de biblioteca
continuam fora do registro. A validação libera seus handles antes das renomeações.

## Evidências

- Reprodução visual no launcher antigo: Forza aparecia “Pronto para a My Games”;
  conferir a preparação mudava o estado para “Preparar”.
- Build Release do launcher/supervisor e testes concluído.
- Regressão inicial do fluxo: **43 asserções**, cobrindo registro pelo jogo e pela
  dashboard, repetição, cancelamento, dependência/executável ausentes, build
  divergente e preservação de conteúdo instalado, recibos, perfis e saves.
- Regressões focadas de biblioteca/preflight: **180 asserções em 8 casos**.
- Teste pela interface: selecionar Retail 17559 e usar “Conferir e iniciar”.
  O próprio aplicativo publicou o registro e a cópia verificada do Forza
  (2.432 arquivos originais; 7,32 GB com o header). Nenhuma edição manual do
  catálogo ou chamada avulsa de registro foi usada nessa validação.
- Operação `.local/evidence/logs/desktop/8687593`: confirmou
  `X360N_LAUNCH_VERIFIED` e abriu a interface original da 17559. A etapa até o
  início da sessão levou aproximadamente 115 segundos, incluindo a primeira
  cópia. Isso não é uma medição comparativa de desempenho nem promessa de tempo
  para outros equipamentos.
- Sessão `20261021T094735Z-session-0-dash_17559`: encerrada com código 0,
  43.911 ms de execução. Existem mensagens de serviços ainda não implementados;
  código 0 sozinho não certifica compatibilidade.
- Após detectar entrada do usuário, o agente interrompeu o controle das janelas
  a pedido dele. O mantenedor confirmou: **“ta tudo funcionando na questao dash
  e rodar my games e forza”**. Isso aprova manualmente esse fluxo do launcher.

## Pendências e limites

Depois do primeiro build foram acrescentadas guardas para receitas não importadas
ou sem AOT, preservação do contexto completo do catálogo durante o registro e
reuso da primeira verificação quando nada precisa ser copiado, com novos casos
de teste. A recompilação final foi recusada pelo lease do ambiente durante a
sessão manual do mantenedor; nenhum artefato foi alterado nessa tentativa.
Após o encerramento do jogo, o alvo de testes foi recompilado em Release:
**59 asserções do fluxo passaram**, incluindo receitas não importadas/sem AOT e
dashboard sem opção de biblioteca. Posteriormente, a consulta de processos confirmou
launcher e jogos encerrados; os executáveis finais foram recompilados em Release.
Nenhum processo do usuário foi fechado pelo agente.

Não foi validado um pacote distribuído em máquina limpa nesta rodada. O fluxo
implementado pertence ao modelo e frontend compartilhados do produto.
O aceite de conquistas permanece separado: desbloqueio verdadeiro, notificação
original, Gamerscore no Guide e persistência após reiniciar ainda precisam ser
confirmados. Nenhuma aprovação de commit/push foi recebida.

## Erro posterior ao criar perfil na 17559

O mantenedor relatou queda ao finalizar a criação do perfil. A análise somente
de leitura encontrou, na sessão `20261021T095029Z-session-0-dash_17559`:

- 06:51:05.838: criação local retorna sucesso.
- 06:51:05.844: login local retorna sucesso.
- 06:51:05.913: leitura guest inválida em `0x00000005`, thread guest
  `0xF80002CC`; encerramento `STATUS_ACCESS_VIOLATION`.
- `profiles/local-v1.dat` existe com 33 bytes, tamanho esperado do formato v1,
  gravado no horário da criação. Nenhum perfil foi alterado pelo agente.
- `report.json` contém `crash: null`: a sessão não habilitou captura de pilha.
  As mensagens de `XamProfileOpen` e `DASHUSER:` próximas à queda não provam
  causalidade. Ainda não há função/instrução culpada identificada nem correção
  certificada para essa falha. O fluxo de criação de perfil permanece reprovado.

## Tela cheia e diagnóstico pelo aplicativo

A janela persistente do supervisor era sempre uma janela com bordas de 1280×720;
o fullscreen do runtime SDL não se aplica quando ele recebe uma janela pai.
A opção **Iniciar sessão em tela cheia**, em Configurações, agora é persistida
em `graphics.toml` e aplicada à janela da sessão, sem bordas, nos limites do
monitor. Essa janela permanece durante as transições dashboard/jogo. Configurações
antigas continuam em modo janela. Não houve troca de backend nem alteração do SDK.

Diagnóstico oferece **Capturar pilha de falhas nas próximas sessões**, desligado
por padrão. A opção encaminha `--crash-stack` ao supervisor; ela não corrige
nenhum acesso inválido. Nenhuma nova sessão de jogo/dashboard foi iniciada nesta
etapa e o controle das janelas do usuário continua suspenso.

Build final em `.local/build/desktop-only`, Release, via `scripts/devshell.cmd`,
concluído para aplicativo, supervisor e testes. Resultados focados:

- Modelo `[renderer],[launcher-flow]`: 79 asserções em 4 casos passaram.
- Interface `[session-fullscreen],[launcher-options],[launcher-layout],[session-event],[launcher-ui]`:
  63.918 asserções em 15 casos passaram. As fixtures usam janelas próprias ocultas,
  sem jogos. Verificam persistência da opção, estilo e dimensões da janela,
  layout, eventos e erros de preparação.
- A primeira rodada da interface encontrou uma mensagem genérica que omitia
  `dash.xex` quando o preflight falhava; o detalhe foi preservado e a repetição
  acima passou. Testes não foram enfraquecidos.
- Uma tentativa anterior em snapshot isolado falhou por configuração clang-cl
  sem SSSE3; não é evidência de build limpo aprovado. O build final usou a
  configuração existente clang++ Release com `-march=x86-64-v2`.

Logs, hashes e manifestos em `.local/evidence/validation/launcher-fullscreen-20261021/`.
Tela cheia ainda não recebeu aceite visual em uma sessão real.

## Queda do Forza relatada pelo mantenedor

A sessão `20261021T095251Z-session-1-forza_horizon` terminou às 06:57:53.056,
após 302.052 ms, com `STATUS_ACCESS_VIOLATION` (`0xC0000005`): leitura guest
`0x02A2C6F8`, thread guest `0xF8000054`. Não foi timeout nem cancelamento do
launcher. `report.json` também contém `crash: null`, sem endereço da instrução
ou pilha nativa. Os avisos de GPU anteriores não provam a causa. Essa queda e
a criação de perfil permanecem problemas abertos; nenhuma correção especulativa
ou bypass foi aplicado. Os logs existentes foram preservados com SHA-256 no
manifesto `observed-crashes.json`.

## Reabertura do launcher — correção compilada

O estado inicial ignorava o resultado positivo dos recibos e mantinha “Conferir”.
O frontend agora reaproveita esse resultado para exibir preparação pronta e
“Iniciar Xbox 360”, sem adicionar varreduras/hashes de inventários à abertura.
Recibos desatualizados e pendências conhecidas continuam pedindo preparação.
As verificações existentes antes da execução permanecem; esta alteração não
constitui otimização medida do tempo de lançamento.
Foi acrescentado um teste focado de reabertura, recibo desatualizado e executável
ausente. Após o mantenedor encerrar a sessão, o build Release do aplicativo
e dos testes concluiu. `[launcher-ready],[launcher-ui]`: 113 asserções em 10
casos passaram (`ready-tests.log`). A primeira tentativa havia sido recusada
pelo lease, código 73; nenhum processo do usuário foi encerrado pelo agente.
