# DASH-OS-FEATURES-03 — Storage

Incremento sobre `main`; alterações paralelas de Guide e empacotamento preservadas.
Sem interfaces próprias, alterações de originais, dados reais, push ou Release.

## Implementação

- `0220`: cancelamento real de cópia/movimentação por OVERLAPPED, registrado
  por ContentManager/runtime. Verificação entre blocos, inclusive sem callback
  ou após PROGRESS_QUIET; `wait=1` espera limpeza/conclusão mesmo sem evento.
  Cancelamento antes do commit preserva a origem e remove a preparação parcial.
  Ponteiros inválidos são recusados; I/O pendente sem cancelador registrado
  retorna ERROR_NOT_SUPPORTED, sem afirmar sucesso fictício.
- `0221`: FE/`0x2000C` usa o mesmo backend de transferência. Layout de 552
  bytes confirmado no wrapper original 17559 `816EEA78`, já preservado em
  `.local/evidence/storage-p0/copy-request.txt`: owner +0, dispositivo +8,
  item +16, OVERLAPPED +528, flags +532, callback/contexto +536/+540.
  Flags 1/2/4 significam mover/dispositivo/owner; argumento adicional não
  comprovado (+544) não zero retorna ERROR_NOT_SUPPORTED. XMsg aceita o
  pedido assíncrono sem concluir o OVERLAPPED antecipadamente.

Preservados o backend transacional anterior, progresso original de 64 bits,
isolamento C7/C8, conflitos, quotas e destino completo quando a limpeza de
uma movimentação falha. Cancelamento que chega após o início do commit pode
concluir a operação; não remove um destino já comprometido para tentar
desfazer uma origem parcialmente excluída.

## Evidências focadas

- Antes de 0220: cancelamento retornava sucesso, mas concluía a cópia;
  **4/20 verificações falharam**. Após 0220: 42/42 no caso inicial.
- Antes de 0221: FE/`2000C` retornava `80004005`; controle novo falhou 1/6.
- Build Release e conjunto final `[storage-transfer],[storage-device],
  [save-storage],[profile-content],[aggregate-content]`: **1.030 asserções,
  15 casos aprovados**. Inclui cancelamento de copy/move, espera sem evento,
  reutilização do OVERLAPPED, pedido FE síncrono/assíncrono/cancelado, quota,
  conflito, persistência C8 e recuperação de falha na exclusão da origem.
- Replay isolado dos **25 patches/5 arquivos** afetados sobre o SDK pinado
  `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`: SHA-256 finais idênticos.
  Primeira tentativa diferenciou somente CRLF do Git local; repetição com
  `core.autocrlf=false` verificou os bytes exatos.
- As primeiras compilações detectaram uma declaração na classe errada e um
  nome de constante inexistente; corrigidos antes de executar os testes finais.

Logs: `.local/storage-*-build.log`, `.local/storage-features-tests.log`,
`.local/storage-message-red.log` e
`.local/evidence/validation/storage-replay-1793333394423683900/result.json`.

## Aceite e escopo restante

Preparação oficial de 0220/0221 concluída em `.local/storage-features-prepare-final.log`.
A primeira rodada foi recusada corretamente porque o README dos patches foi
alterado durante o build; esse arquivo também integra a identidade do SDK.

Computer Use autorizado pelo mantenedor para todo este trabalho, sem novo
pedido de permissão. A captura da janela embutida expirou com `Computer Use
app approval timed out`; as sessões separadas foram capturadas após ativar a
janela retornada por `list_apps`. Sessões limitadas a 300 s, sem jogos.
A XUI original exibiu HDD/MU/USB, capacidades, categorias, perfil Player1,
save, proprietário e opções Copiar/Mover/Excluir. O seletor original abriu e
permitiu destacar a MU; **a cópia ainda não produziu conteúdo no destino**.
Não há aceite de cópia/movimentação/exclusão pela interface nesta rodada.

Dados isolados em `.local/diagnostics/storage-transfer-17559-20261030T043140Z-e369c1b7c66f41f792a71ec70036150b`.
Foi copiado somente o perfil de teste Player1 de um diagnóstico anterior,
com os quatro arquivos de origem preservados por SHA-256. O save sintético
foi associado ao XUID desse perfil, sem tocar dados reais. Registros em
`20261030T151226Z-session-0-dash_17559`, `20261030T152330Z-storage-ui` e
`20261030T153154Z-storage-ui`. Ler também `rex.1.log`–`rex.3.log`: os logs
rotacionam, portanto somente `rex.log` não preserva toda a operação.

O parâmetro `dash17559_signin_ui` apenas força um pedido de Sign-in no primeiro
quadro; **não habilita/desabilita os serviços XUI normais**. Não foi alterado.
Um pedido forçado retornou `80004001`; não é prova da causa do bloqueio de cópia.

## Recuperação de quota — 0222

Controle novo reproduziu ERROR_DISK_FULL (112) ao repetir uma cópia de 300 kB
numa MU de 1 MiB contendo 900 kB abandonados pelo processo anterior. A limpeza
ocorria depois da consulta de espaço. 0222 limpa antes da consulta, com registro
de staging por volume e exclusão mútua apenas no início/fim da transferência.
Um segundo caso verifica outra unidade recuperando enquanto a primeira copia.
O conteúdo recuperado é lido e enumerado após reiniciar o runtime.

Build Release e conjunto final: **1.054 asserções/17 casos aprovados**.
Replay atualizado: **26 patches/5 arquivos**, todos os SHA-256 idênticos.
Logs `.local/storage-recovery-tests.log`, `.local/storage-stale-quota-red.log`,
`.local/evidence/validation/storage-replay-1793374933296375800/result.json`.
Diagnóstico de retorno do seletor no host 17559 é opcional e desligado normalmente;
os novos registros de submissão/conclusão XMsg exigem log DEBUG.
Novo preparo e rastreamento do retorno da XUI seguem em andamento.

Não há novo aceite de conquistas/GamerCard, configurações, múltiplos usuários,
1888 ou 6717. Esses itens não foram ampliados antes de concluir Storage.
