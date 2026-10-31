# DASH-OS-FEATURES-03 — Storage e imagens

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

## Consulta de atributos Windows — 0228

Cada ancestral era consultado por `symlink_status` e novamente por
`GetFileAttributesW`. 0228 usa a consulta nativa para reparse points e erros;
mantém todos os ancestrais, componentes inexistentes permitidos para criação,
demais falhas negadas e o caminho anterior nas outras plataformas. Junction real,
arquivo abaixo dela e descendente inexistente rejeitados: **12 verificações
antes/depois**, fora do sandbox que bloqueia sua criação.

Mesmo cenário e executor protegido de 0227, após 0228:
**4.825,85 / 4.466,78 / 4.444,73 ms**. Mediana −47,14% sobre 8.450,17 ms;
gate local de 6.000 ms rejeita a referência e aprova a variante. Não extrapolar
para desktop sem sandbox ou FPS. Logs `.local/storage-path-attributes-*.log`.
Regressões de Storage fora do sandbox: **1.337 asserções/21 casos aprovados**;
seus tempos não são comparados com a medição protegida. Replay: **16 patches/1
arquivo**, SHA-256 conferido em `storage-replay-1793395964964730000/result.json`.
Preparo oficial concluído (`.local/storage-path-attributes-prepare.log`).
Em `20261030T215426Z-storage-ui`, fora do sandbox e com as quotas originais do
fixture, a XUI excluiu o save de 256 MiB da MU após Excluir → Sim, removeu a
linha e mostrou 0 KB na categoria e 510 MiB livres (antes 254 MiB). Origem HDD:
256 hashes e header intactos; pacote/header da MU ausentes, sem staging; save
pequeno do USB intacto. `native-attributes-ui-verification.json` registra isso.
A inspeção posterior do assistente de transferência mostrou seleção de conteúdo
e permitiu desmarcar Player1, mas não iniciou outra transferência. O supervisor
encerrou pelo limite de **300.204 ms**, sem exceção: não houve encerramento normal
nem aceite de cancelamento de I/O. Não comparar seu tempo com a sessão protegida.

## Recuperação entre publicação do pacote e header — 0229

O controle novo reproduziu perda do header ao reiniciar entre os dois renames:
**6/40 verificações falharam**, com nome de fallback e staging restante
(`.local/storage-commit-recovery-red.log`). 0229 fecha os arquivos antes de
renomear o header para uma intenção de commit com checksum XXH3-128. Antes da
primeira consulta de quota/catálogo do volume, publica o pacote e o header
pendentes. Cópias parciais sem intenção são descartadas. Falha ao publicar o
header preserva o pacote completo e a intenção para nova tentativa.

Não substitui arquivos existentes. Checksum inválido, dispositivo/nome inválido,
conflito ou junction preservam a pendência e tornam o volume indisponível; a causa
real fica no log. O checksum detecta corrupção, não autentica conteúdo. A
recuperação permanece restrita ao staging privado desse volume, sem seguir links.
Estado de recuperação pertence ao runtime; quotas continuam sendo recalculadas.

**185 asserções/3 casos novos aprovados**, incluindo três processos filhos
encerrados por `_Exit(93)` nos pontos reais: intenção pronta, pacote publicado,
header publicado. Cada filho tem limite de 20 s; reinício confere bytes completos,
metadados opacos, origem preservada e staging ausente. Negativos conferem sentinel,
preservação do conflito e recuperação após reparar a pendência descartável.
Conjunto final: **1.522 asserções/24 casos aprovados**, log
`.local/storage-commit-recovery-regression.log`, fora do sandbox para junction real.
Replay: **17 patches/2 arquivos**, SHA-256 conferido em
`storage-replay-1793399698170083000/result.json`. O build detectou e corrigiu um
cast de enum antes destes testes. Wrappers próprios concluídos foram encerrados
após verificar ausência de compiladores ativos.

Limite: comprova interrupção de processo, sem promessa de durabilidade a corte de
energia. A exclusão parcial da origem de uma movimentação interrompida ainda não
é atômica; o destino completo é preservado. Cancelamento gráfico durante I/O
continua sem aceite. Conquistas/P1 não ampliados enquanto Storage está em curso.

Preparo oficial aprovado (`.local/storage-commit-recovery-prepare.log`). Sessão
`20261030T230207Z-storage-recovery-ui`: uma cópia da massa descartável recebeu
interrupção real após publicar o pacote, sem header final. Ao reiniciar a 17559,
a MU apareceu com 509 MB livres e a XUI listou **“Transfer save”**, tipo Jogo Salvo,
sob Jogo/Perfil Desconhecido (title/XUID sintéticos sem cadastro). Exibiu 2 MB
arredondados para 1.048.579 bytes de payload. Sessão de consulta encerrada
normalmente em **76.495 ms**, exit 0, sem exceção/timeout. Continuam os erros
`FC/58003`; não é aceite integral dos serviços da dashboard.
`storage-recovery-ui-verification.json` confere SHA-256 do header completo
`cf55eaa33216daa354a225c0387bc89491cd314b96006643aa3326ca16138db8`,
payload, origem da cópia e massa anterior preservados, staging ausente.

Escala protegida após 0229: **4.648,15 / 4.370,45 / 4.612,38 ms**, 56 asserções
aprovadas, `.local/storage-commit-recovery-scale.log`. Mediana 4.612,38 ms contra
4.466,78 ms de 0228 (+3,26% nesta rodada); sem ganho medido e sem inferência de
mudança estável ou de desempenho gráfico. Não houve compilação/sessão gráfica
simultânea à medição.

## Limite do cancelamento gráfico com 480 MiB

Massa nova: 1.024 arquivos × 480 KiB, Player1 descartável, quotas preservadas.
`20261030T230612Z-storage-cancel-ui`: cópia HDD → MU concluída, encerramento normal
em 192.357 ms. `20261030T231006Z-storage-bulk-cancel-ui`: assistente original,
perfil desmarcado, move HDD → USB; B abriu “Parar Transferência”, Sim chamou
`XMsgCancelIORequest(407816F0,1)` às 20:14:25.225, resultado da movimentação 0
às 20:14:25.863. Encerramento normal em 273.923 ms.

Tentativa com confirmação antecipada `20261030T231613Z-storage-bulk-cancel-ui`:
request às 20:20:28.446, cancel `(407500F0,1)` às 20:20:42.123, resultado 0 às
20:20:44.573; XUI mostrou 1/1 transferido com êxito. A conferência final atingiu
300.195 ms, sem exceção, portanto essa sessão terminou por timeout. Não houve
aceite de cancelamento antes do commit nem novas repetições gráficas.
`storage-cancel-ui-final-verification.json` confirma 2.048 hashes, os headers
completos de MU/USB, origem removida após sucesso, perfil intacto e staging
ausente. Cópia USB anterior foi arquivada antes de restaurar a origem de teste.
Enumeração de volumes e resposta da XUI ainda lentas com ambos os volumes cheios;
não confundir barra atrasada com I/O ainda cancelável. Estes resultados não
comprovam a hipótese sobre o instante exato de início do commit.

## Ancestrais na varredura de quota — 0230

`QueryContentDeviceSpace` chamava `IsUnlinkedStoragePath` em cada arquivo: além
da entrada, consultava repetidamente os mesmos pais até a raiz do Windows. 0230
valida a raiz/ancestrais antes da varredura e cada entrada antes de o iterador
descer nela. Continua rejeitando reparse points/junctions no Windows e symlinks
no POSIX, com quota fresca, reservas e separação HDD/volumes secundários.

Teste novo com junction real aninhada e ancestral de volume: **28 asserções
aprovadas antes/depois**, incluindo snapshot inalterado no erro e nova consulta
após retirar somente a junction. Mesmo teste de escala protegido de 0229:
antes **4.648,15 / 4.370,45 / 4.612,38 ms**; depois
**453,757 / 454,023 / 504,485 ms**. Mediana −90,16% (10,16×), 56 asserções
aprovadas. Gate local optativo de 1.000 ms recusa a referência e aprova a variante;
sem limite temporal novo no CI. Sem compilação ou sessão gráfica simultânea.

Conjunto focado fora do sandbox: **1.550 asserções/25 casos aprovados**, log
`.local/storage-quota-ancestors-regression.log`; tempos desse executor não são
comparados aos protegidos. Replay de **18 patches/1 arquivo**, SHA-256 conferido
em `storage-replay-1793402815940691700/result.json`. Nenhuma afirmação de FPS,
ganho gráfico ou validação POSIX a partir destes testes Windows.

Preparo oficial concluído (`.local/storage-quota-ancestors-prepare.log`).
`20261030T234552Z-storage-quota-cancel-ui`, massa nova com **8.192 × 60 KiB**
(480 MiB), mesmos limites lógicos de HDD/MU/USB: assistente original HDD → USB,
Player1 desmarcado, Iniciar → B → “Parar Transferência” → Sim. Request às
20:48:53.244; `XMsgCancelIORequest(40760A90,1)` às 20:49:03.974; resultado real
**1223** às 20:49:05.716 (`rex.1.log`). XUI mostrou **0 de 1 transferidos com êxito**.
Encerramento normal em **210.017 ms**, exit 0, sem exceção ou timeout. Permanecem
as mensagens não implementadas `FC/58003`.

`storage-quota-ui-verification.json`: 8.192 hashes e header da origem intactos,
perfil preservado, pacote/header de destino ausentes e staging removido. É aceite
de cancelamento pelo fluxo original, sem retardador de I/O, mudança de retorno ou
instrumentação no loop de cópia. Os conjuntos de arquivos das sessões diferem;
não são uma comparação equivalente de desempenho gráfico. Medição de ganho
permanece restrita ao teste protegido acima.

## Recuperação da origem de move — 0231

Reprodução focada: após publicar o destino, `DeleteContent` removia primeiro o
payload da origem. Um header aberto no Windows recusava a exclusão com erro 5,
deixando a origem sem payload. Controle anterior: **5 falhas/62 asserções** em
dois casos (`storage-retirement-red.log`), incluindo os quatro checkpoints de
origem ainda ausentes. Não é resultado aprovado.

0231 retira pacote e header da origem para staging privado, com journal de
intenção fechado e checksum. Antes do marcador de commit, recupera os nomes
originais; depois dele, só termina a limpeza privada. Nunca substitui conteúdo
novo que ocupe o nome antigo. Falha ao retirar o header restaura o pacote;
falha ao limpar bytes privados retorna o erro real e permite nova recuperação
na próxima consulta ao dispositivo. O destino completo permanece em ambos os
casos. O callback Remove segue a remoção lógica efetiva, inclusive se a limpeza
privada falhar depois; não é emitido após rollback. Sem endereços específicos de
versão no serviço compartilhado, telas próprias ou alteração de cancelamento.

**272 asserções/4 casos aprovados**, com interrupção real de subprocessos
limitados nos quatro pontos da origem, header bloqueado, limpeza privada parcial,
payload privado bloqueado, conflitos com conteúdo novo e journal corrompido.
Reparar/liberar somente a fixture permite nova tentativa no mesmo runtime.
Conjunto final: **1.803 asserções/28 casos focados aprovados**,
`.local/storage-retirement-regression.log`; junctions reais fora do sandbox.
Replay: **24 patches/3 arquivos**, hashes conferidos a partir do SDK pinado em
`storage-replay-1793405240513241900/result.json`.

Limites: recuperação de interrupção de processo, sem promessa de durabilidade
a corte de energia. `DeleteContent` direto continua com o contrato anterior;
esta alteração cobre especificamente a retirada da origem de move.

Preparo oficial aprovado (`.local/storage-retirement-prepare.log`). A fixture
descartável recebeu interrupção real em `SourceHeaderRetired` (exit 93), com
pacote/header da origem privados e destino completo. Na sessão nativa
`20261031T002726Z-storage-retirement-ui`, a XUI original listou **“Transfer save”**
no HDD restaurado e na MU, ambos Jogo Salvo/2 MB arredondados, sob título/perfil
desconhecidos (IDs sintéticos). MU com 509 MB livres. Encerramento normal em
**93.539 ms**, exit 0, sem exceção/timeout. Persistem erros `FC/58003`; não é
aceite integral dos serviços originais.

`storage-retirement-ui-verification.json` confere origem, destino, perfil e
ausência de staging. SHA-256 do payload em ambos:
`7e31e5962974900441e745f400f9dc5060c7a21acfd97f197b4cbece12fed3d8`;
headers origem/destino respectivamente
`2d1085a44126adb0bfcd3902aefbb0961c6811f87564738bca0ea915bab1eeb4` e
`cf55eaa33216daa354a225c0387bc89491cd314b96006643aa3326ca16138db8`.

## Exclusão direta recuperável — 0232

Reprodução: `DeleteContent` com header realmente bloqueado retornava erro 5
depois de apagar o pacote público. **1 falha/6 asserções** no controle anterior
(`storage-delete-recovery-red.log`). A exclusão direta e desmontar/excluir agora
usam a mesma retirada privada recuperável do move. Preservam os bloqueios de
conteúdo aberto/perfil ativo e os contratos de conteúdo inexistente, só header,
só pacote e pacote como arquivo regular. O journal cobre com checksum quais
componentes existiam, recusando também conteúdo novo criado no nome que antes
estava ausente. XAM só emite Remove após a remoção lógica efetiva, inclusive se
a limpeza privada retornar erro real posteriormente.

**615 asserções/11 casos focados aprovados** em exclusão, move e callbacks,
incluindo interrupções reais limitadas antes/depois do commit, header/payload
bloqueados, formas de conteúdo, conflitos e recuperação após remover somente
o impedimento da fixture. **2.084 asserções/34 casos** na regressão relevante,
`.local/storage-delete-recovery-regression.log`; C8 com reinício/dois XUIDs e
junctions reais aprovados, sem suíte completa. Replay final de **25 patches/3
arquivos**, hashes conferidos em
`storage-replay-1793407061417661800/result.json`.

Preparo oficial aprovado (`storage-delete-recovery-prepare.log`). Sessão
`20261031T005718Z-storage-delete-recovery-ui`: fixture recebeu interrupção real
em `SourceHeaderRetired` durante exclusão, exit 93. A 17559 recuperou e listou
“Transfer save” no HDD; hashes do pacote/header conferidos antes da ação. Fluxo
original **Excluir → Sim** terminou com **“Nenhum item encontrado”** na mesma
sessão, sem alteração manual de lista/cache. Encerramento normal em
**104.311 ms**, exit 0, sem exceção/timeout. Persistem mensagens `FC/58003`.

`storage-delete-recovery-ui-verification.json` confere restauração íntegra antes
da ação e, após encerrar o processo, pacote/header ausentes, perfil preservado e
staging ausente. Não comprova durabilidade a corte de energia. Wrappers próprios
concluídos foram encerrados após recusar qualquer árvore com worker nativo ativo.

## Gamerpic persistida — 0233

Sessão de diagnóstico `20261031T010418Z-gamerpic-contract-ui`, encerrada
normalmente em **77.127 ms**, confirmou pedidos reais do cache original:
tipo **10**, título 0, XUID completo `E0031A573AB1A3BF`, usuário **FE**; GamerCard
tipo **2**, título `FFFE07D1`, tile 0, usuário 0. A imagem estava ausente; os
dados existentes mostraram um jogo, 10 G e uma conquista, sem criar desbloqueios.
O leitor recusava todo tipo maior que 1. Controle: **38 falhas/80 asserções**
na primeira versão dos testes de imagem.

0233 lê `pp_64.png`/`pp_32.png` pessoais e `tile_64.png`/`tile_32.png` públicos
do pacote do perfil local conhecido. Usuário FE seleciona o XUID inteiro;
o pedido público usa a conta conectada. Arquivos e perfis não substituem uns
aos outros. Os quatro nomes constam em ASCII na própria XAM 17559, em
`8162FDD0`, `8162FDDC`, `8162FDE8` e `8162FDF4`; comparação adicional com
[UserProfile do Xenia Canary](https://github.com/xenia-canary/xenia-canary/blob/canary_experimental/src/xenia/kernel/xam/user_profile.h);
o contrato FE foi observado na própria 17559. Dispositivo atual e proteção contra links
continuam validados. Arquivo vazio, não regular, bloqueado ou maior que 16 MiB
é recusado; o decoder existente valida PNG e os limites da textura.

**252 asserções/3 casos novos aprovados**: arquivos reais temporários, dois
XUIDs, reinício, bytes encoded, pixels ARGB e limites, erros síncronos/assíncronos
e bloqueio real Windows. **755 asserções/11 casos** de regressão focada de
GamerCard, chaves, perfis e APIs de conquistas aprovados; sem suíte completa.
Replay de **24 patches/1 arquivo**, hash conferido em
`storage-replay-1793409529869708600/result.json`.

Preparo oficial aprovado (`profile-picture-prepare.log`). A fixture preserva os
18 arquivos anteriores e recebeu somente dois PNGs originais 64×64 da XAM 17559,
copiados para os arquivos pessoais/públicos do perfil descartável. Fonte:
imagem carregada da XAM com SHA-256
`235c5f6aa71a98ec7ec94d7e825198bf97c38099af9bb174ad2ab0642b182d07`.
PNG pessoal no offset `5BEB47`, SHA-256
`3ff8da2d7d197666b2eafe9195cf19320bc1c11ae9516b5de977bc07ec8fa346`;
público no offset `5D5976`, SHA-256
`c9933428f6cd173fe87d195dd2cc7450aaf7e287c625db167306a89d660ac9ab`.
Bytes extraídos e seus CRCs PNG foram conferidos, sem edição das imagens.

`20261031T014006Z-gamerpic-local-ui`: imagem pessoal apareceu junto a Player1
na dashboard; a imagem pública distinta apareceu no GamerCard original. Dados
existentes mantidos: um jogo, **10 G e uma conquista**. A pessoal já estava
visível no início; não foi necessário abrir o Guide para esse aceite.
Encerramento normal em **61.580 ms**, exit 0, sem exceção/timeout. Persistem
mensagens não implementadas e `DASHUSER` ausente; não é aceite integral da XAM.
`gamerpic-ui-verification.json` confere os pedidos reais, os dois PNGs e todos
os 18 arquivos anteriores, tanto na fonte quanto na cópia. Não certifica seleção
ou gravação de imagem pela XUI nem notificação de nova conquista.

## Reabertura do aviso de conquista — 17559

O mantenedor esclareceu o sintoma: o primeiro aviso apareceu; as conquistas
seguintes fizeram som e constaram no histórico/Gamerscore, mas sem aviso visual.
O defeito relatado é de apresentação, não de gravação dos desbloqueios.

O original `816B7C28` compara a thread atual do KPCR (`r13+256`) com o worker
do compositor em `81AA09F0`. `816C7D00` só emite o comando `D1OVL_ENABLE` se
essa identidade coincide; em outra thread apenas registra a solicitação para
o loop XAM. O host compõe sincronicamente na thread de VdSwap e não executa
esse loop. Após a expiração desabilitar o plano, a próxima solicitação podia
continuar atualizando cena/som sem reabilitar a imagem.

`SystemCompositorContext` vincula a identidade apenas durante a composição de
notificações sem modal e restaura o valor anterior ao retornar ou desenrolar
uma exceção. Os comandos continuam vindo do compositor original. Guide,
navegação, recursos, persistência e a fila de desbloqueios não foram alterados;
endereços ficam exclusivamente no perfil 17559. Não há alocação, passe gráfico,
instrumentação ou trabalho adicional em frames sem notificação ativa.

Reprodução focada, sem entrypoint, GPU, perfil ou desbloqueio: a opção
`--compositor-rearm` do probe carrega a XAM original verificada e chama sua
rotina real nas transições `1 → 0 → 1 → 0 → 1`. Sem o vínculo, todas as cinco
transições produziram zero bytes de comandos (exit 7). Com a correção, cada
transição produziu os 40 bytes originais, com registrador/valor e shadow
conferidos e identidade restaurada (exit 0). Logs locais
`notification-rearm-red.log` e `notification-rearm-original_rearm.log`.
O teste de escopo passou dez asserções, incluindo duas threads, aninhamento e
exceção; o conjunto de regressão focado passou **150 asserções/3 casos**
(`notification-rearm-regression.log`). Isso comprova o rearmamento dos comandos;
o aceite visual de uma conquista seguinte no jogo permanece separado.

O probe anterior de construtor/fila também passou, preservando a cópia própria
do texto pelo original (`notification-rearm-original-queue.log`). Preparo oficial
17559 concluído (`notification-rearm-prepare.log`); recibo conferido por SHA-256
para executável, host de sessão e as duas fontes de produção alteradas
(`notification-rearm-prepared-verification.json`). Nenhuma conquista existente
foi apagada ou reemitida para produzir evidência visual.

## Gravação de Gamerpic e PNG original — 17559

0234 implementa o modo PNG bruto observado no export `2FB/81670740`:
buffers 64×64/32×32, argumentos PPC em `SP+54/5C/64`, seleção pública/pessoal,
chave persistida e notificação original `0E` após commit. Valida dimensões,
CRCs e decodificação antes de gravar os bytes recebidos no pacote do XUID.
Conclusão síncrona/OVERLAPPED comunica os erros reais. Modos de catálogo,
premiação e Avatar continuam retornando não suportado; o export legado deixou
de concluir sucesso sem gravar imagem. Não é aceite completo do seletor XUI.

A gravação guarda snapshots novos/anteriores e intent com checksum em staging
privado. Interrupção antes do commit restaura imagens/chave; depois dele apenas
limpa arquivos privados. Conflitos e corrupção são preservados e recusados,
sem sobrescrever conteúdo novo. Leituras/chave recuperam trabalho pendente,
invalidam cache obsoleto e repetem limpeza bloqueada. A quota considera snapshots,
crescimento dos nomes públicos e um temporário; é consultada novamente após
recuperação. Proteções contra links permanecem. Não certifica corte de energia
nem escritores concorrentes de consoles independentes.

`XamPngEncode`/`Ex` do perfil 17559 chamam o encoder libpng recompilado original
`816B0BA8`, preservando pitch, alpha, HRESULT e conclusão. A consulta original
de cancelamento `816B8090` acessava TLS de tarefa inexistente no worker host
(AV reproduzida); agora consulta a tarefa real do runtime e balanceia seu handle,
mantendo fallback original para workers originais. Endereços permanecem somente
na 17559; nenhum binário ou recurso original foi editado.

Evidência focada: o teste da API nova falhou sem 0234 (20 falhas/43 asserções).
O conjunto final passou **1.142 asserções/19 casos**, incluindo GamerCard,
conquistas offline, imagens, chave, PNG, cancelamento, bloqueio real de destino,
limpeza bloqueada e quota (`gamerpic-picture-verified-tests.log`). O script
`tests/local/profile-pictures-process.ps1` passou sete encerramentos reais com
exit 93, reinício independente, rollback/commit, intent corrompido e conflito
público (`gamerpic-picture-process-final.log`). Todas as fixtures são próprias.
Replay desde o SDK pinado: **51 patches/4 arquivos**, SHA-256 conferidos
(`gamerpic-picture-write-final-replay.log`).

Probe sem entrypoint/GPU: imagem XAM 17559 verificada, PNG opaco e com alpha,
pitch não compacto e buffer insuficiente passaram, com pixels e canários
conferidos. O fluxo encoder original → `XamWriteGamerTileEx` → leitura pública/
pessoal passou para 64/32, preservando bytes e pixels
(`gamerpic-picture-roundtrip.log`). A tentativa gráfica anterior
`gamerpic-write-contract-ui` não certifica navegação: a captura/ativação não
correspondeu à dashboard; não foram enviados comandos nessa condição e apenas
o processo diagnóstico próprio foi encerrado. Aceite do fluxo de gravação pela
XUI e da próxima notificação real continuam pendentes.

Preparo oficial final aprovado (`gamerpic-picture-write-final-prepare.log`),
com quatro artefatos nativos e quatro fontes/lock conferidos no recibo; SHA do
patch conferido pelo lock verificado (`gamerpic-picture-prepared-verification.json`).
O probe integrado voltou a passar com a XAM recém-preparada
(`gamerpic-picture-prepared-roundtrip.log`). O primeiro preparo fora recusado
por uma edição documental própria no README dos patches durante o build;
o candidato final foi repetido com essa closure congelada.

`20261031T050921Z-gamerpic-write-ui`: a XUI exibiu a pessoal na dashboard,
pública distinta no Perfil e **um jogo/10 G/uma conquista**. Configurações →
Perfil → Editar Perfil → Imagem do Jogador abriu o seletor original, com um
item sem imagem; pedidos reais de tiles 4/13 e catálogo agregado com zero itens
constam no log. Selecionar não comprovou gravação; retorno ao editor funcionou.
Esse bloqueio de catálogo permanece, sem imagem substituta nem aceite fictício.
Fechamento normal por Alt+F4 em **170.444 ms**, exit 0, sem exceção/timeout.
Os 25 arquivos da fonte e dez arquivos protegidos do perfil privado, incluindo
imagens, conta e histórico, continuam idênticos por SHA-256
(`gamerpic-picture-ui-verification.json`). Persistem stubs e mensagens XLIVEBASE
não implementadas; a sessão não certifica cobertura integral da XAM.
