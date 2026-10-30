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

Preparação oficial e sessão gráfica: em andamento nesta etapa.
Não há novo aceite de conquistas/GamerCard, configurações, múltiplos usuários,
1888 ou 6717. Esses itens não foram ampliados antes de concluir Storage.
