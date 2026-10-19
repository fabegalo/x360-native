# Retail 1888 — permanência do Guide (19/10/2026)

Escopo inicial: corrigir o painel original do Guide que aparecia e deslizava
para fora da tela sem entrada. O complemento abaixo trata a falha de reabertura
relatada no aceite manual e acrescenta o patch compartilhado 0101. Preservadas
as alterações locais de serviços que já estavam em andamento, os originais,
o isolamento de versões e os perfis/saves reais. Não houve execução de jogos.

## Complemento — reabertura e posse do atalho

O mantenedor reprovou o build inicial: só a primeira abertura funcionava; a
segunda aparecia vazia e deixava a interface presa. O log da sessão
`20261019T065634Z-session-0-dash_1888` mostra a segunda cena criada normalmente
às 03:56:51.631 e a troca para `createprofile.xex` às 03:56:51.821. As pressões
posteriores de Guide eram ignoradas porque o host só fechava `hud.xex`.
Isso não foi uma aprovação manual do build anterior.

Os ensaios CPU anteriores não cobriam os timers reais: executar `XuiAnimRun`
com delta fixo não avança o relógio usado pelos timers originais de 800 ms.
Além disso, os eventos eram entregues juntos durante a abertura. A sonda agora
envia eventos espaçados, desenha a cena CPU a cada frame, dá tempo aos timers e
recusa mudanças involuntárias de aplicação. O teste com timers
`20261019T070301Z-1888-guide-delayed-timers` falhou com exit 13. A tentativa
`20261019T071349Z-1888-guide-ownership` excedeu o limite antigo de 5 s da sonda;
esse limite do fixture foi ajustado para 10 s, com supervisor limitado a 15 s.

A conclusão anterior de que nenhuma captura do gesto era necessária foi
revogada. O descarte geral de `guide_input.h` continua removido. O patch
`0101-system-ui-opening-input.patch` captura o estado dos controles na troca
de posse: somente os controles já pressionados ficam com o gesto inicial até
seu UP, incluindo eventos atrasados e repetições. Novos botões são entregues;
START/BACK voltam a ser comandos normais após a soltura. As duas ABIs HUD usam
o mesmo contrato, sem drenar indiscriminadamente a fila. O host captura também
o atalho de fechamento antes de devolver a entrada à dashboard e passa a aceitar
Guide para fechar qualquer aplicativo de sistema ativo, inclusive Create Profile.

A regressão `[guide-opening]` falhou no SDK anterior em 24 das 88 asserções.
Após a correção e a extensão para o gesto de fechamento, os filtros
`[guide-opening],[guide-input],[profile-ui]` passaram com **269 asserções em
10 casos**. O adaptador 1888 passou novamente (51 asserções/1 caso).
O ensaio `20261019T072926Z-1888-guide-delayed-green` passou em 6,461 s, com
quatro eventos entregues, três cenas, 360 frames e timers reais, sem janela.

O diagnóstico de destruição confirmou que o callback original já destrói a
cena do aplicativo (`remaining object=0`); não foi acrescentada outra destruição
manual. O ciclo `20261019T072001Z-1888-guide-cycle-ui-red` abriu e fechou três
instâncias sem entrada física; a terceira foi observada completa na tela.
Esse teste isola a recriação da cena, sem provar o atalho do controle.

O mantenedor autorizou explicitamente Computer Use para executar o fluxo.
As primeiras teclas enviadas pela ferramenta não produziram ações reconhecidas;
essa tentativa não conta como navegação validada. O fixture integrado opt-in
`dash1888_input_probe` entrega pelo backend real do runtime três sequências de
GUIDE+BACK+START, com soltura e repetição entre aberturas; modo 1 fecha pelo
atalho, modo 2 fecha por B. A dashboard original continua responsável por cenas,
timers, entrada e desenho GPU. O driver não abre dispositivos físicos, usa dados
descartáveis e só existe quando o parâmetro diagnóstico é selecionado. Padrão 0.

Uma reconstrução foi recusada pelo gate porque os fontes de diagnóstico foram
alterados durante a preparação; nenhum recibo foi publicado para essa tentativa.
Validação visual integrada e recertificação final: sem aceite consolidado nesta
etapa. A revisão posterior abaixo não executou build nem UI.

## Revisão local para commit — sem build ou UI

O mantenedor pediu revisar todas as alterações locais, procurar código morto e
regressões e fazer commit, expressamente sem build ou abertura de interface.
Essa autorização substitui a espera por confirmação de commit registrada no
fechamento anterior; não constitui aceite visual nem autoriza push.

Foram corrigidas duas saídas antecipadas de `Open`: falha de alocação do buffer
e recusa de `BeginSystemUi`. Ambas descarregavam a aplicação depois de registrar
seu recurso em `8192C040`, sem limpar a entrada. Agora chamam `8192C060` antes do
detach/unload, como já ocorria na falha posterior de criação da cena. A tabela
original armazena o handle do módulo; deixá-lo registrado após unload pode
encaminhar uma consulta de recurso para um módulo já liberado. Também saíram
um alias e um include sem uso; o novo teste de patch XEX e seu vínculo com
`bcrypt` ficaram condicionados a Windows.

Os cinco `*/generated/rexglue.cmake` emitidos junto aos manifests auxiliares não
são incluídos pelo host: este usa seu próprio CMake e `generated/<app>/sources.cmake`.
Permanecem locais/ignorados, evitando versionar cinco cópias inativas do template.
Os 121 adaptadores de imports têm referências nas tabelas geradas correspondentes;
foram preservados. O log solto `c72-profile.log` foi movido para a pasta de
evidências, sem descartar seu conteúdo. Corrigidos links relativos da pesquisa
e o resumo desatualizado do STATUS (100 patches, até 0101).

Verificações novas, sem compilação nem execução de dashboard/jogo/testes nativos:

- `apply-sdk-patches.ps1 -Check`: todos os patches e hashes finais conferidos.
- Reaplicação dos 100 patches em cópia descartável das revisões fixadas do SDK
  e SDL: 106 arquivos finais coincidem por SHA-256 com o lock. A preparação da
  cópia foi ajustada para incluir arquivos criados por patches, o submódulo SDL
  e os atributos originais de fim de linha; falhas dessa preparação não foram
  contadas como regressões do produto. Nenhuma edição foi feita no SDK real.
- Os 22 originais do perfil 1888 coincidem com os SHA-256 da preparação e do
  guard; os cinco manifests auxiliares, análises e DLLs esperadas concordam.
- Os 15 symlinks materializados do libmspack coincidem com seus alvos.
- Sintaxe PowerShell da sonda de firmware conferida. `git diff --cached --check`
  dos fontes/documentação aprovado; os arquivos `.patch` preservam os espaços
  obrigatórios de linhas vazias de contexto, que o check textual do contêiner
  acusa como trailing whitespace. Suas linhas adicionadas foram verificadas
  separadamente; bytes e hashes dos patches permaneceram intactos.

Evidências e scripts da revisão: `.local/evidence/local-review-20261019/`.
Foram conferidos os logs anteriores de entrada (269 asserções/10 casos),
contratos locais (942/24) e patch XEX (2.844/13), sem repetir essas execuções.
As correções desta revisão **não foram compiladas nem executadas**; os recibos
anteriores não certificam os fontes modificados. Reabertura com controle físico,
sign-in, criação de perfis, teclado e demais serviços continuam sem aceite completo.

## Revisão pós-commit `bf1c1a9` — correções e atalho BACK+START

A revisão do commit encontrou problemas, corrigidos nesta etapa:

| Achado | Correção | Evidência |
|---|---|---|
| 0101 trocava LB/RB na captura (`0x5804` é RSHOULDER) | Tabela única `PadKeysFromState` | Caso `Shoulder buttons…` em `[guide-opening]` |
| START do atalho BACK+START executava na dashboard e o Guide era trocado pela ação dela | Filtro `guide_chord_ms` no SDK; 150 ms nas duas dashboards por padrão | `[guide-chord]`: título, HUD, toque curto, janela expirada, desligado |
| 0094 fazia `REX_FATAL` em routines ≠ 1/5 (antes: saída limpa) | Todo routine fecha a plataforma pelo handoff, com aviso | Sonda de firmware com routines 1, 5 e 2; `[firmware-exit]` |
| 0099 escrevia em `*r3` sem ABI confirmada | Falha sem escrever; ABI do único chamador (HUD 1888 `913E31E8`) documentada | `[custom-banner]` com memória intocada |
| `crset`/`crclr` sem builder (mesma família do 0096) | Builders e fixtures novos | `sdkcheck.CrAliases`: 24.576 verificações |
| `apply-patch` mais restrito que o loader | Cauda AES não alinhada e `img_src_off == 0` aceitos como no SDK | `apply-patch accepts layouts the SDK loader accepts` |
| Guide só no controle 0 | As duas dashboards consultam os quatro controles | Sem teste automatizado; aceite manual pendente |

Causa do atalho: o controle do mantenedor (8BitDo, VID `0x2DC8`, conforme o log
`20261019T082354Z-session-0-dash_1888`) só sinaliza GUIDE junto com o segundo
botão. Nos traços `20261019T052843Z` e `20261019T053310Z`, a HUD recebeu apenas
transições posteriores de BACK/START; o primeiro DOWN já tinha ido para a
dashboard. Na sessão `082354Z`, a Create Profile substituiu o Guide 77 ms depois
de ele abrir. Isso é compatível com o START da dashboard sem perfis, mas o
solicitante não foi registrado: hipótese, não fato. Com o filtro, o primeiro
DOWN de BACK/START espera até o GUIDE, a soltura ou o fim da janela. A janela de
150 ms é uma hipótese a calibrar e acrescenta até esse atraso a BACK/START nas
dashboards (toques curtos são entregues na soltura).

Não mudaram: `XamAllocSize` (todos os consumidores conhecidos são realloc dentro
da memória comprometida; documentação corrigida), a falha de abertura após a
cena (o callback original já destrói a cena, como registrado acima) e o duplo
SHA-1 de recursos cifrados (otimização exige medição antes/depois; pendente).
Motocross ainda tem traps `crmove`/`crnot` gerados antes do 0096 e precisa de
codegen `--ignore-stamp` mais regressão própria.

Testes focados, Debug, 19/10: SDK `[guide-opening],[guide-chord],[guide-input],
[profile-ui],[custom-banner],[local-logon-array],[xam-allocation]` 549 asserções
em 15 casos; projeto `[firmware-exit],[xex-patch]` 3.008 em 13; `CrAliases`
24.576; sonda de firmware 1/5/2; `1888-native-bridges` 51. Reaplicação dos 100
patches numa cópia do SDK fixado: 105 arquivos idênticos ao lock (os dois patches
SDL não se aplicam à cópia sem submódulo); `apply-sdk-patches.ps1 -Check` exit 0.
Logs em `.local/logs/review-fix-*.txt`. `scripts/build-title.ps1` recompilou
`dash_1888` (exit 0, 06:11) e `dash_17559` (exit 0, 06:19, estágio 4 em 168 s);
os dois executáveis e seus `rexruntime.dll` contêm o novo cvar. Log:
`.local/logs/review-fix-dashes.log`. Não houve suíte completa nem execução de
dashboard; o aceite do atalho com o controle físico é do mantenedor.

## Causa e correção

O processamento de cada frame chamava `818D21E8`, interpretando `hud+0x38` como
um diálogo pendente. Esse campo contém a cena persistente do host. No XAM 1888,
`818D21E8` encaminha para `818D1AE0`: solicita a transição de fechamento e aguarda
seu término. O worker original usa essa rotina ao encerrar uma aplicação, não
como atualização de uma aplicação aberta.

O rastro `20261019T062531Z-1888-guide-host-caller` registrou o host tocando a
faixa 1–44 e, imediatamente, a faixa 45–74, ambas por `818D0564`. Não houve uma
entrada do usuário nem uma solicitação de saída da cena do Guide.

`ProcessApplicationInput` agora executa o callback original `8000000A` e uma
passagem da fila XUI. A chamada de fechamento e a segunda leitura redundante
de entrada saíram do caminho por frame. Os rastreios temporários
`hud_trace_tmp.cpp` e `xam_trace_tmp.cpp` foram removidos do produto; suas cópias
e os logs de diagnóstico permanecem somente em `.local/evidence/dash1888/`.

## Regressão focada

O modo explícito `service-guide-animation` da sonda CPU usa os módulos originais
verificados, uma fila real de entrada sem drivers físicos e dados descartáveis.
Executa o mesmo processamento de entrada do produto, os timers e as animações
originais com passos de 1/60 s. Confere o estado do host a cada frame, registra
o desenho CPU e destrói a cena original. Não cria janela nem apresenta frames.

| Evidência em `.local/evidence/runs/` | Resultado |
|---|---|
| `20261019T062936Z-1888-guide-animation-red-valid` | Falha esperada: estado 1 → 0 no frame 0; exit 13 |
| `20261019T063029Z-1888-guide-animation-green` | 120 frames com host aberto, desenho CPU e destruição da cena; exit 0 |
| `20261019T063800Z-1888-guide-animation-final` | 3 ciclos de abertura/destruição, 360 frames e 3 desenhos CPU com as DLLs finais sem rastreios; exit 0 |

As tentativas iniciais da nova sonda falharam por ausência de backend de entrada
no ambiente sem janela; foram corrigidas com uma fila vazia, sem simular sucesso
de dispositivos. Uma inclusão de header também expôs a macro Windows `small`;
o nome local foi corrigido. Uma execução acidental do binário anterior após essa
falha de compilação foi descartada e não constitui validação.

Os dois testes de descarte de entrada deixados pendentes foram inicialmente
compilados após corrigir macros `X_ERROR_*`: 3 casos / 56 asserções aprovados.
Esses dois casos só demonstravam o esvaziamento da fila; foram removidos junto
com o helper após a comparação funcional abaixo. Não houve suíte completa nem
regressão de outra dashboard.

## Validação de BACK+START e retirada de `guide_input.h`

BACK+START é o atalho intencional do mantenedor nas duas dashboards. A presença
dos dois botões no log não demonstra defeito do controle. O fluxo 17559 solicita
o Guide sem esvaziar a fila; as duas versões usam a mesma infraestrutura SDK
de posse de entrada. Essa infraestrutura não descarta automaticamente o aperto
de abertura: suas latches protegem principalmente o retorno da entrada ao título.

Foi feita comparação CPU com o HUD 1888 original e o processamento por frame já
corrigido. Um driver sintético entrega DOWN/UP de BACK e START, sem acessar o
controle físico. Os logs identificam cada evento e o chamador guest. Os quatro
eventos foram consumidos, incluindo BACK pela fila XUI; o teste não depende de
uma fila vazia ou de entrada desabilitada.

| Run | Resultado |
|---|---|
| `20261019T064207Z-1888-guide-chord-raw-traced` | BACK→START, sem helper: 4 eventos entregues; 3 ciclos / 360 frames; aprovado |
| `20261019T064349Z-1888-guide-chord-reversed` | START→BACK, sem helper: 4 eventos entregues; 3 ciclos / 360 frames; aprovado |
| `20261019T064429Z-1888-guide-chord-consumed-control` | Mesma sequência com helper: aprovado, sem benefício demonstrado |

O helper esvaziava até 64 eventos indiscriminadamente, inclusive botões que não
fazem parte do atalho. Sua necessidade não foi demonstrada após corrigir o loop
de frames. Foram retirados `guide_input.h`, a chamada na abertura e os testes que
apenas reproduziam esse descarte. Permanecem regressões com o HUD original nas
duas ordens do atalho, exigindo a entrega dos quatro eventos. Não houve novo
teste do controle físico, nem execução da 17559 por essa comparação.

## Aceite e limites

O Release sem os rastreios foi certificado pelo driver oficial, com validação
de inputs, produtos, produtor, inventário e estado incremental aprovada. Os 92
arquivos originais do inventário anterior permaneceram com os mesmos SHA-256.

A execução autorizada `20261019T063630Z-1888-guide-panel-final` durou 30,159 s e
terminou por timeout do supervisor, sem crash, erros críticos ou chamadas AOT
não registradas. As capturas `guide-10s.png` e `guide-18s.png`, em
`.local/evidence/dash1888/guide-fix-20261019/visual-final/`, mostram o painel aberto
com o relógio avançando. Essa execução foi anterior à retirada do helper e não
usou entrada física; o defeito UTC/`(null)` permaneceu visível. As duas mensagens
XMP conhecidas permaneceram no log; não foram ocultadas.

Após retirar o helper, o driver oficial terminou com exit 0. A validação final
do recibo passou em Release, inputs, produtos, produtor, inventário e estado
incremental, sem marcador de preparação incompleta. SHA-256 do executável:
`f3454265562935c1abb770d0dd2ab960bc99c64910a56d9670c4e2d09b67995a`.
O SDK continua fixado em `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`.

`sdkcheck.1888-native-bridges` passou com 51 asserções em 1 caso, após recompilar
os alvos de teste em Release. As sondas finais, com as DLLs certificadas,
`20261019T065307Z-1888-guide-chord-final-raw` e
`20261019T065321Z-1888-guide-chord-final-reversed`, passaram com exit 0, quatro
eventos entregues por execução, três ciclos e 360 frames cada. Os 92 originais
foram novamente conferidos por SHA-256, sem alterações. Evidências em
`.local/evidence/dash1888/guide-fix-20261019/` e nas respectivas pastas de run.

## Revisão das outras adaptações locais

Foi revisado o caminho de entrada, abertura, fechamento, carregamento, memória,
imports e registro de recursos da 1888, além dos patches SDK 0093–0100 e dos
contratos de saída associados. Não foi encontrado outro esvaziamento
indiscriminado de entrada nem outro fechamento executado por frame nos caminhos
revisados. A retirada do helper e dos rastreios temporários não descarta o
trabalho de serviços que já estava pendente no workspace.

| Adaptação mantida | Fundamentação e limite |
|---|---|
| Imports XUI e alocação privada | Encaminham à implementação original 1888 ou ao serviço real do SDK. Os ensaios anteriores de construtores, tamanho de alocação e desenho CPU têm controles negativos; não substituem a interface por sucesso fabricado. |
| Inicialização/display/composição | Reproduzem os contratos do host original dentro do callback de swap do SDK. O descritor vazio de `Hide` corresponde a `818DACB8`; a sincronização de apresentação pertence ao SDK, sem executar o worker completo de XAM. O aceite visual continua restrito ao ensaio documentado. |
| Limpeza em `Close` | `818D174C` chama `818D0D80` com a cena em `hud+0x38`; a rotina esvazia a lista de diálogos em `scene+0x68`. É usada no encerramento, distinta da transição de fechamento retirada do loop de frames. |
| Registro VK | `818D9F58` registra o recurso antes de tentar o slot; o adaptador preserva essa ordem e a posse do callback do aplicativo pai. Não foi invertida a ordem apenas por haver retorno de erro no registro do slot. |
| Requests de apps/perfis | Convertem somente layouts identificados da 1888, copiam a memória da requisição e rejeitam payloads não suportados. Sucesso significa enfileiramento, sem fabricar login ou criação de perfil. 51 asserções finais dos adaptadores passaram. |
| `XamProfileOpen`, banner e estatísticas de dispositivo | Falhas explícitas para serviços não implementados, com contrato de saída documentado/testado; não retornam sucesso de stub. Dicionário de perfil e estatísticas privadas seguem sem implementação. |
| Patches SDK 0093–0098 | Correções genéricas de SHA-1, logon/saída, chaves legadas, aliases PPC, tamanho de alocação e recursos XEX cifrados; lock e patches verificados no build. As evidências anteriores incluem testes independentes e negativos, sem afrouxar a identidade dos originais. |
| Firmware/17559 | A mudança existente na 17559 acrescenta espera de saída do chamador para firmware, acompanhando o novo contrato do supervisor; não altera o atalho ou o consumo de entrada. Não houve execução adicional da 17559. |

Foram reutilizadas as evidências anteriores em `services/`: SHA-1 (639
asserções/4 casos), aliases PPC (16.384 verificações), serviços UI (451/4),
contratos locais no diretório CTest (942/24), supervisor/isolamento (223/4) e
patch XEX (2.844/13). O log anterior `runtime-contracts-green.txt` contém uma
falha; não foi contado como aprovação. A execução posterior no diretório CTest,
`runtime-contracts-ctest-workdir.txt`, passou. Esses resultados históricos não
foram repetidos nem apresentados como testes novos deste incremento.

O diff rastreado passou em `git diff --check`. Nenhum commit ou push foi feito;
o próximo gate é o teste manual do mantenedor com o controle físico no build
acima. Esta revisão não constitui certificação integral de todos os serviços.

Este incremento não certifica Guide completo, sign-in/criação de perfis,
teclado virtual, jogos ou encerramento pelo menu original. Relógio/AM–PM,
mensagens XMP `7002F`/`70044` e o recurso localizado `pt-br/shrdres.xzp` ausente
continuam pendências explícitas. Não há comparação de desempenho neste ensaio.

Procedência dos originais e referência anterior:
[perfil e preparação](dashboard-1888-2026-10-18.md) e
[intro original](dashboard-1888-intro-2026-10-18.md).
