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
Preparo oficial com 0222 e observadores concluído. A sessão
`20261030T172919Z-storage-ui` comprovou o bloqueio: `FC/58072` retornava
`80004005`, propagado pela inicialização de `OperationProgress.xur`, que
terminava sem handle e antes de submeter qualquer transferência. Cancelamento
de I/O já concluído retornou zero; não era a causa.

## Cálculo original de buffers — 17559

O dispatcher FC original (`818CCE08`, caso `818CD4E0`) chama `818E1828`.
Esse método não usa `this`: analisa os filtros e calcula os buffers de URI,
headers, schemas e tarefa. O host 17559 passa somente essa mensagem ao método
original verificado, preservando erros; as demais mensagens seguem no SDK.
Também usa `XamLookupCommonStringByIndex` original (`816E0BF0`) para os botões.
Os observadores de cena/controlador/progresso são opcionais e desligados normalmente.

Teste CPU `x360n_storage_17559_probe --web-task-size`: cinco verificações
aprovadas, incluindo repetibilidade, acréscimo de memória por flag, schema
válido e rejeição de tipo inválido (`80004001`, saída zero), com canários
preservados. Tamanhos observados: 10.100, 282.276 e 10.380 bytes.
Logs `.local/storage-web-size-{build,probe}.log`.
Preparo oficial concluído (`.local/storage-web-native-prepare.log`). Na sessão
`20261030T175127Z-storage-ui`, as sete consultas reais retornaram sucesso com
tamanhos entre 36.024 e 314.536 bytes. A inicialização e criação da tela original
de progresso retornaram zero, com handle válido. A XUI exibiu a barra e seu
diálogo original de falha; a transferência ainda não foi submetida porque a
consulta de metadados do save retornou `ERROR_NOT_SUPPORTED` (50). O serviço
atual só atende pacotes executáveis. Esse é o próximo bloqueio reproduzido.

Não há novo aceite de conquistas/GamerCard, configurações, múltiplos usuários,
1888 ou 6717. Esses itens não foram ampliados antes de concluir Storage.

## Metadados de saves e perfis locais — 0223

O controle `[local-content-metadata]` falhou antes do patch com retorno 50.
0223 projeta identidade/nome do header persistido, tamanho da árvore instalada
e miniatura armazenada, sem inventar versões XEX, certificados ou licenças.
Verifica a identidade do header antes de publicar o resultado.
Controle novo: 122 asserções aprovadas; conjunto focado afetado: **1.178
asserções/18 casos aprovados**, incluindo reinício, sync/async e erros sem
alterar a saída. Replay do arquivo alterado: **12 patches/1 arquivo**, SHA-256
final conferido em `storage-replay-1793383313989767100/result.json`.
Logs `.local/storage-local-metadata-{red,tests,regression}.log`.
Preparo oficial concluído em `.local/storage-local-metadata-prepare.log`.
Na sessão `20261030T181654Z-storage-ui`, a consulta do save retornou zero
na listagem e na tela de progresso. A XUI passou à verificação de permissões
e exibiu "Impossível Transferir Item": o byte de atributos está zerado.
`922FF3CC..922FF474` exige a flag de transferência de dispositivo (`0x40`)
para esse fluxo. Nenhum destino foi criado; cópia visual ainda sem aceite.

## Transferência local pela XUI — 0224

O controle novo comprovou cópia real de save e perfil HDD → MU e leitura após
reiniciar, enquanto dez verificações da permissão anunciada falharam (flags=0).
0224 anuncia somente a transferência entre dispositivos (`0x40`) desses pacotes
locais. Não habilita reatribuição de perfil nem altera permissões STFS assinadas.
Release: 146 asserções no caso novo; **1.198 asserções/18 casos** no conjunto focado.
Replay: **13 patches/1 arquivo**, SHA-256 conferido em
`storage-replay-1793384919001732200/result.json`. Logs
`.local/storage-transfer-flag-{red,tests,regression,prepare}.log`.

Preparo oficial concluído. Na sessão `20261030T184405Z-storage-ui` a XUI original
copiou o save de 1 MiB HDD → MU7 e moveu HDD → USB12; callbacks/progresso e I/O
concluíram com zero. A origem foi preservada na cópia e removida no move.
Os dois destinos têm SHA-256
`30E14955EBF1352266DC2FF8067E68104607E750ABB9D3B36582B8AF909FCB58`.
Após reiniciar (`20261030T184835Z-storage-ui`), a MU exibiu o save com nome,
tamanho e proprietário preservados. Excluir abriu a confirmação original,
inicialmente em Não; após escolher Sim removeu pacote e header da MU, mantendo
a cópia USB com o mesmo hash. As sessões encerraram normalmente em 236.352 ms e
291.589 ms, sem exceção não tratada; os logs ainda contêm stubs/erros preexistentes.

**Aceite parcial:** a lista mantém a linha após excluir; reabrir o jogo
após excluir mostra o item inválido. O callback `XamContentRegisterChangeCallback`
ainda é stub. Seu registro e os eventos reais precisam ser implementados antes
do aceite completo da atualização da lista. Progresso/cancelamento gráfico em
transferência maior, falhas durante o commit, jogos/outros tipos de conteúdo e
gestão completa de perfis continuam sem aceite. Gamerpic/conquistas e P1 pendentes.

## Eventos reais para o cache original — 0225

A sessão de exclusão reproduziu a linha obsoleta mesmo reabrindo a lista. O
registro `XamContentRegisterChangeCallback` era stub. O original 17559 registra
um callback único em `8167CCC0`; `81684E90` passa motivo e item de 512 bytes.
Os caminhos `81685700`/`81686790` comprovam removido=1/adicionado=0.

0225 implementa o registro por runtime e emite os eventos de copiar/mover/excluir
pelos exports XAM reais, após a alteração correspondente. Tamanho/data de criação
vêm dos atributos persistidos. O callback executa com restauração do contexto
PPC; zero desregistra. A dashboard original continua responsável pelo cache e
pela tela. Se o destino já foi confirmado e a limpeza da origem falha, anuncia
o destino e preserva o erro. Não emite eventos por conflito/cancelamento anterior
ao commit, nem afirma que uma origem parcialmente removida foi excluída com sucesso.

Controle antes do patch: 3 verificações de eventos falharam (31/34 passaram;
as operações de arquivo já eram reais). Após: **67 asserções aprovadas**, incluindo
reentrada para consultar a existência durante o callback e move assíncrono.
Conjunto afetado: **1.269 asserções/19 casos aprovados**. Replay dos quatro
arquivos alterados: **32 patches**, SHA-256 idênticos, em
`storage-replay-1793387060211883200/result.json`. Logs
`.local/storage-change-{red,tests,regression}.log`. O primeiro build encontrou
qualificação de namespace ausente em dois chamadores; corrigida antes do build
final. Preparo oficial concluído (`.local/storage-change-prepare.log`), refeito
após alteração concorrente de `deps.lock.json` para Windows ARM64; essa alteração
foi preservada. O preparo dos observadores reutilizou o SDK.

Em `20261030T194803Z-storage-ui`, a cópia USB → MU produziu o mesmo hash, mas a
MU continuou vazia no cache. Encerrou normalmente em 264.891 ms, sem exceção.
Observadores opcionais/desligados normalmente em `original_xam_services.cpp`
e no perfil de análise confirmaram em `20261030T201434Z-storage-ui`: registro
`92263530`, evento removido=1, device=7, title=4D5309C9, manager=92969878.
O callback chega ao original, que falha ao alocar sua mensagem de 528 bytes:
`XamAllocEx: unsupported flags 08000000`. Não chega ao consumidor `92264B44`;
a linha continua visível. **O cache visual ainda não está corrigido.**

A massa descartável de 256 MiB/4.096 arquivos atrasou a inicialização; foi
preservada fora do volume ativo. Outra com 256 arquivos reproduziu leitura lenta
com quota lógica de HDD, atingindo o limite de 300 s antes da exclusão em
`20261030T200853Z-storage-ui`. Para isolar o callback, somente o HDD descartável
passou a usar a capacidade física real; MU/USB mantiveram suas quotas. O custo
isolado da varredura permanece por medir/corrigir. Cancelamento gráfico sem aceite.


## Alocação virtual da mensagem — 0226

A falha do callback foi isolada: chega a `92263530`, mas `92263398` recebe
`80004001` ao pedir `XamAlloc(0x08000000,528,out)`. O original `816E67E8`
encaminha essa classe à alocação virtual (`816E5F10`). 0226 acrescenta somente
essa classe ao adaptador existente, com memória guest real, gravável e liberável;
classes físicas/desconhecidas continuam retornando erro.

O controle novo falhou em **6/61 verificações** antes do patch, incluindo os
quatro resultados de eventos quando o callback tenta alocar/copiar/liberar sua
mensagem como o original. Após: **91 verificações/2 casos aprovados**; regressões
focadas de alocação e Storage: **1.439 asserções/21 casos**. Replay do alocador:
**10 patches/1 arquivo**, SHA-256 conferido em
`storage-replay-1793391974320837700/result.json`. Logs
`.local/storage-message-alloc-{red,tests,regression}.log`.
Preparo oficial concluído (`.local/storage-message-alloc-prepare.log`). Em
`20261030T204628Z-storage-ui`, a XUI original copiou USB → MU e mostrou o save
na MU na mesma sessão (1 MB, Player1). Após Excluir → Sim, mostrou
“Nenhum item encontrado”, sem linha obsoleta. O consumidor original recebeu
reason=1 em `92264B44`, sem falha da classe `08000000`. Pacote e header da MU
ausentes após fechar; origem USB preservada com SHA-256
`30e14955ebf1352266dc2ff8067e68104607e750abb9d3b36582b8af909fcb58`.
Encerramento normal em 239.520 ms, sem exceção; permanecem mensagens
`FC/58003` não implementadas, portanto o log não está livre de erros.

Em `20261030T205102Z-storage-ui`, cópia HDD → MU de 256 MiB/256 arquivos
concluiu com I/O=0 antes do comando B; não é aceite de cancelamento. Add=0 chegou
ao callback e ao consumidor original. Após encerrar normalmente (189.481 ms),
os 256 hashes de cada volume conferiram; header de origem intacto e header de
destino presente. Verificação em `message-allocation-ui-verification.json` na
raiz da massa descartável. Quota lógica de HDD restaurada para 8 GiB depois de
encerrar; não houve alteração no algoritmo de quota nem nos dados reais.

## Custo de resolução com quota lógica — 0227

`ResolvePackagePath` chamava `DeviceRoot` na condição de cada ancestral;
cada chamada recalculava a quota percorrendo todos os arquivos do volume.
0227 resolve esse limite uma vez por chamada, mantendo os testes de todos os
ancestrais, a rejeição de links/junctions e a quota atualizada nas chamadas
seguintes. Não há cache persistente nem dispensa da quota.

Medição Release, mesmo teste/volume sintético: 258 arquivos, 4 consultas de
atributos por amostra, 3 amostras. Antes: **25.862,4 / 24.191,9 / 24.521,4 ms**;
depois: **8.999,79 / 8.450,17 / 8.023,34 ms**. Mediana −65,54% (2,90×).
Correção funcional passou nos dois lados (56 asserções): tamanho real, canários
e redução exata de espaço livre após crescimento de arquivo. Não mede FPS nem
estabelece ganho de tempo gráfico. Logs `.local/storage-path-{before,after}.log`.
Gate local optativo `tests/sdkcheck/storage_path_scale_budget.py`, com limite
explícito de 15.000 ms para essa mediana: referência recusada (exit 1), variante
aprovada (exit 0). O teste normal de correção não impõe limite temporal ao CI.
Regressões pertinentes: **1.325 asserções/20 casos aprovados**; replay do arquivo
afetado: **15 patches/1 arquivo**, hashes conferidos em
`storage-replay-1793394044119081000/result.json`. Preparo oficial concluído
(`.local/storage-path-prepare.log`). A compilação unitária linkou e seus testes
passaram; o wrapper remanescente sem compiladores ativos foi encerrado.

`20261030T212124Z-storage-ui`: com HDD lógico de 8 GiB e MU de 512 MiB,
dispositivos apareceram com 7,7 GiB/254 MiB livres, e a categoria Jogos da MU
mostrou os 256 MiB persistidos. Enumeração ainda lenta; não houve exclusão grande
nessa sessão. Encerramento normal em 286.868 ms, sem exceção ou timeout.
Não é benchmark gráfico equivalente nem aceite de cancelamento.
