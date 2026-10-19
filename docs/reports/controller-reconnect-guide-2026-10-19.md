# Reconexão e BACK+START — 19/10/2026

Escopo P0: entrada do controle e gesto do Guide nas Retail 1888/17559. Aceite
físico obrigatório ainda pendente. Nenhum jogo ou sessão visual foi iniciado
pelo agente; sem push, tag ou Release.

## Base e diagnóstico

O pedido indicava `8d1e86a`; o checkout já estava em `44cd00f`, com os ajustes
posteriores de biblioteca/importação. Durante o trabalho entrou o commit
documental `5be3d99`. Todos foram preservados, assim como a revisão pinada
ReXGlue `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2` e LLVM 22.1.8.

O mantenedor esclareceu que a falha de reconexão foi observada na **17559**.
Há duas evidências distintas, que não devem ser confundidas:

- A sonda nova, sem janela, reproduziu `0xC0000005` em 566 ms com quatro
  consumidores concorrentes e mudanças de conexão. Pilha: `memmove` →
  `SlotAssignment::OnDevicesChanged` → `InputSystem::RefreshDevices` →
  `InputSystem::SetState`. A enumeração reconstruía `devices_`, `device_owners_`
  e os vetores de atribuição sem sincronização. Outro consumidor podia ler
  memória invalidada pela realocação. O mutex do patch 0102 protegia os latches,
  mas não essas operações. Evidência: run
  `20261019T130408Z-input-p0-concurrency-red`, `crash.txt` e `report.json`.
- A sessão física recente `20261019T124722Z-session-0-dash_17559` registra
  remoção às 09:49:10.529, novo dispositivo às 09:49:12.525, pedido do Guide às
  09:49:12.710 e `XamLoaderRebootToDash` às 09:49:13.695. Saída **0**, handoff
  tipo 4, sem exceção. É um encerramento solicitado pelo guest após reconectar,
  não evidência de acesso inválido nessa sessão. Sua relação com entrada
  indevida deve ser confirmada pelo teste físico; não foi inventada uma pilha
  para esse encerramento.

Os três crashes recentes da **1888** têm outra assinatura: `0xC0000409`, chamada
de `0x92164098` para `0x92164008` após descarregar `createprofile.xex`. São
evidência separada de ciclo de vida da aplicação de perfil, não reprodução do
crash de reconexão da 17559. Essa falha de createprofile não foi corrigida nem
recebeu aceite nesta tarefa.

## Caminho do gesto e correções

Nos logs físicos anteriores (`20261019T052843Z` e `053310Z`) o 8BitDo
`2DC8:310A` aparece pelo backend SDL/XInput; START/BACK chegam antes ou durante
GUIDE. Não há evidência de que o driver tenha fabricado um botão A: os originais
tratam **VK_START 0x5814 e VK_A 0x5800 como confirmação** na XUI. Isso está no
ramo `0x8191DF74` da 1888 e `0x817CB0A0` da 17559. BACK `0x5815` participa do
caminho de cancelamento. GUIDE não tem keystroke próprio no SDL; os hosts
consultam seu bit de estado e solicitam a aplicação original correspondente.

| Etapa | Estado, evento e consumidor |
| --- | --- |
| Primeiro BACK/START | SDL mantém os bits físicos; a API XAM do título retém estado e DOWN na janela configurada. |
| Segundo botão/Guide | O bit GUIDE chega ao poll do host; captura dos controles precede o pedido `0x21015`. O Guide dedicado usa a mesma borda. |
| Carga da aplicação | `BeginSystemUi` acumula a captura anterior, mesmo se o controle já estiver solto. |
| Callback original de entrada | HUD/HUD Ex consomem DOWN/repeat/UP do gesto. Sem filtragem, o ramo XUI de START confirma a seleção nas duas versões. |
| Fechamento | A borda pede fechamento; `EndSystemUi` captura os controles ainda segurados antes de devolver entrada à dashboard. |
| Soltura e nova pressão | UP ou fila vazia com estado solto libera a máscara; um novo A/BACK/START segue o contrato normal. |
| Remoção/troca | InputSystem invalida bordas, máscara e DOWN pendente; uma conexão nova já segurando Guide precisa soltá-lo antes de gerar nova borda. |

O contrato de consulta privilegiada de estado (`0x80000000`) continua bruto,
incluindo Guide. A posse dos comandos é filtrada nas APIs de keystroke do HUD e
no estado do título; o poll do host precisa enxergar o gesto físico.

O patch 0101 capturava os botões ao iniciar a aplicação, mas o carregamento
podia terminar depois da soltura física. Uma segunda captura apagava a máscara
de abertura e liberava o START pendente para o HUD. A regressão nova reproduziu
o vazamento nos quatro índices: oito asserções falharam, entregando `0x5814`
onde apenas o novo A deveria chegar. A 17559 também não capturava o atalho de
fechamento como o host 1888 fazia.

- **0103:** uma única exclusão mútua recursiva por InputSystem protege
  enumeração, atribuição, ponteiros dos drivers, consulta e invalidação. Shutdown
  retira os drivers sob a trava e os destrói fora dela, sem esperar a UI com a
  trava de entrada retida. Perda de um dispositivo invalida o estado mesmo se
  houver teclado/dispositivo sintético no mesmo usuário.
- **0104:** os hosts usam a mesma operação de borda/captura. O estado da borda
  pertence à conexão; GUIDE já pressionado ao conectar precisa ser solto antes
  de abrir/fechar a UI. Captura antes do carregamento preserva DOWN/repeat/UP do
  gesto até seu consumo, sem descartar controles novos. O fechamento registra
  os botões naquele instante, sem confundir uma pressão posterior de A com a
  UI antiga. Fila vazia e estado solto liberam máscaras de abertura para drivers
  que geram transições sob demanda e nunca produziram o UP correspondente.
  Um contador de geração detecta hotplug entre GetState e GetKeystroke: o
  primeiro evento da conexão nova é classificado com seu próprio estado,
  consultado novamente somente quando a geração mudou.
  A limpeza por fila vazia só consulta o estado quando existe máscara de
  abertura pendente, evitando acrescentar uma consulta ao polling sem gesto.
- **0105:** SDL remove o observador e libera seus subsistemas/handles também no
  destrutor; callbacks pendentes não capturam o driver. Extração e aplicação dos
  lotes de eventos são serializadas para impedir ultrapassagem de ADDED por
  REMOVED. Identificação usa o instance ID armazenado; chegada expirada e
  duplicada são toleradas. Soltura cancela repeat antes da emissão.

Os dois hosts habilitam por padrão a janela existente de **150 ms nos quatro
índices**, respeitando valores explicitamente configurados. O SDK isolado
continua com padrão 0. BACK/START isolados são entregues ao soltar ou ao expirar
a janela; A e o botão Guide dedicado não esperam por ela. A janela é uma
heurística temporal: não recupera um comando já entregue antes de seu prazo.
Seu ajuste físico continua parte do aceite, sem alegação de latência ou
performance melhor. Não há sleeps no caminho de entrada de produção.

## Validação técnica

Evidências locais em `.local/evidence/input-p0-20261019/` e nos runs citados.
As execuções são limitadas, sem controle de aplicativos ou dispositivos físicos.

- Concorrência após 0103: 80.000 consultas completas, quatro consumidores,
  saída 0, nenhuma exceção capturada (run `20261019T131440Z-input-p0-concurrency-green`).
- SDL: o controle positivo confirma attach/remove reais de joystick virtual,
  identidade antiga recusada e eventos de chegada já expirados. O teste de
  destruição falhou antes da correção com saída 10 (subsistema retido) e passou
  após 0105. Há também callback de pump enfileirado por outra thread antes de
  destruir o driver, executado depois da destruição.
- A primeira ampliação dos contratos encontrou duas falhas no cenário de UI
  sem backend: a correção havia alterado o retorno neutro histórico. O contrato
  foi preservado, sem relaxar o teste. Logs da falha foram mantidos.
- Reaplicação inicial: 104 patches em cópia descartável do SDK pinado,
  110 arquivos finais com hashes iguais ao lock, sem edição oculta do SDK.

Rodada final dos contratos Release: **935 asserções em 23 casos**, filtros
`[input-p0],[guide-lifecycle],[guide-chord],[guide-opening],[guide-input],[profile-ui]`.
Inclui configuração explícita 0 e máscara 1, quatro usuários, Guide dedicado,
reconexão aberto/fechado, troca de identidade/backend, eventos pendentes,
BACK/START/A novos e as duas ABIs HUD. Isolamento: 235 asserções/2 casos do
supervisor e 9 asserções/1 caso local com originais. Adaptadores 1888:
51 asserções/1 caso. Logs `contracts-verified.txt`, `isolation.txt`,
`local-isolation.txt` e `1888-bridges.txt`. A rodada final de entrada está em
`contracts-final-pass.txt`; `contracts-accepted-technical.txt` registra a rodada
anterior ao ajuste da consulta em fila vazia, também com 935 asserções.
`contracts-verified.txt` preserva a rodada inicial de 847 asserções.

A revisão final reproduziu outro vazamento ao trocar a identidade entre a
consulta de estado e a de keystroke. A primeira rodada (`between-query-red.txt`)
mostrou três entregas indevidas e uma falha adicional da fixture HUD, que abria
sempre no usuário 0. A fixture passou a receber o proprietário explicitamente,
preservando o teste ANY_USER anterior. O controle de geração em 0104 eliminou
o vazamento; a regressão passou com HUD aberto/fechado nos quatro índices.
As tentativas intermediárias, inclusive erro de compilação da fixture, não
foram contabilizadas como validação. A mudança invalidou os produtos anteriores,
que precisaram de nova preparação oficial.

As sondas anteriores terminaram com saída 0 e nenhuma exceção capturada:
`20261019T132613Z-input-p0-concurrency-final` (80.000 consultas, 853 ms) e
`20261019T132614Z-input-p0-sdl-final` (330 ms). Não são medições de ganho:
o caso concorrente anterior abortava. `sdk-replay-final.txt` confirma novamente
104 patches/110 hashes sobre o pin, em ordem 0103/0104/0105.
Após o ajuste de geração, as sondas passaram novamente:
`20261019T140235Z-input-p0-concurrency-generation` (80.000 consultas, 776 ms) e
`20261019T140236Z-input-p0-sdl-generation` (183 ms). A reaplicação final consta
em `sdk-replay-verified.txt`, também com 104 patches e 110 hashes corretos e
saída 0. `sdk-replay-generation.txt` conserva a verificação intermediária.

Sondas do **Guide original 1888**, sem janela, renderização de apresentação ou
entrada física: `20261019T134715Z-input-p0-1888-original-delayed-correct-path`
(6.930 ms) e `20261019T134742Z-input-p0-1888-original-reversed` (6.927 ms),
ambas saída 0/sem exceção. Cada execução manteve o Guide aberto por 120 frames
originais em três ciclos de criação/destruição. A fonte sintética entregou os
quatro DOWN/UP de BACK/START ao filtro, inclusive eventos atrasados e ordem
inversa. Foram observadas consultas originadas do callback HUD (`LR 913E34FC`)
e do processador XAM (`LR 818D00E8`). Guards de memória e callback de draw CPU
passaram; isso não certifica apresentação visual. Três stubs criptográficos
preexistentes foram registrados por execução, sem alegar cobertura desses serviços.

Com os produtos da revisão final, as sondas passaram novamente:
`20261019T142446Z-input-p0-1888-final-delayed` (9.245 ms) e
`20261019T142455Z-input-p0-1888-final-reversed` (6.891 ms), saída 0 e nenhuma
exceção. Houve compilação concorrente da 17559; esses tempos não são comparação
de performance. Os hashes dos dados de usuário foram conferidos novamente.

A tentativa `20261019T134554Z-input-p0-1888-original-delayed` falhou com saída
13 porque a sonda não encontrava a DLL do HUD fora do diretório do executável.
O ensaio válido acrescentou **somente o diretório nativo da 1888** ao PATH do
processo e usou o arquivo local de chaves existente. Não mudou busca de módulos
em produção nem misturou originais. Os quatro arquivos dos diretórios de usuário
das duas dashboards mantiveram os hashes antes/depois das sondas.

Na **17559**, esta rodada cobre os contratos SDK/HUD Ex, a análise dos callbacks
originais e o build Release. Não houve execução visual do HUD original 17559
pelo agente; reconexão e ausência de Confirmar indevido precisam do aceite manual.

Os dois produtos finais concluíram o driver oficial com saída 0:
`build-dash-1888-final.txt` (573,81 s) e `build-dash-17559-final.txt` (789,75 s).
São durações com builds concorrentes, sem comparação de performance. A
conferência independente `verify-products-final.txt` terminou com saída 0:
identidade, fontes/originais, artefatos, produtor, estado incremental, globs,
flags Release, ausência de marcador incompleto e SDK pinado válidos.

| Produto | Entradas | Artefatos | SHA-256 do executável |
| --- | ---: | ---: | --- |
| Retail 1888 | 3.737 | 10 | `2f6645bcb16aeeb44c4f54c4ca546ce77a7bc903e943d814d3c4ad86221bb27b` |
| Retail 17559 | 3.975 | 12 | `4fae88cdeabd778c4baa765d3c9e2f4eb0a44940baf6c372affd5131b57b6eba` |

Os recibos estão ao lado dos executáveis em `.local/build/titles/dash_1888/`
e `.local/build/titles/dash_17559/`; `product-receipts.json` na pasta de evidências
registra seus hashes completos. Os produtos estão prontos para o teste físico,
sem certificação de apresentação ou aceite manual implícito.

Duas tentativas da 1888 ficaram no wrapper
PowerShell após o processo Ninja já ter encerrado. Foram interrompidas; a mesma
execução oficial fora do sandbox passou dessa etapa, sem alterar ou contornar
gates. Nenhum teste sobre build fracassado foi contabilizado. Isso não representa
validação de jogo ou novo aceite C7/C8, que permanecem nos respectivos relatórios.

Na primeira tentativa da 17559, um processo de codegen de createprofile ficou
sem atividade após produzir seu stamp. Somente esse filho foi encerrado; o driver
registrou falha em 4-build, sem certificar o produto. A repetição foi interrompida
quando a regressão de troca entre consultas exigiu corrigir 0104. Os builds finais
limitam o paralelismo a quatro tarefas por árvore e mantêm todos os gates.
Uma preparação intermediária de cada dashboard foi interrompida para incluir o
guard de fila vazia; somente os recibos produzidos após a revisão final contam.

## Aceite físico obrigatório

Executar nas duas dashboards recompiladas, usando o controle do mantenedor:

1. Desligar/religar repetidamente com Guide fechado e aberto, sem crash,
   encerramento da plataforma ou comando antigo.
2. Abrir/fechar com BACK+START, sem Confirmar, Start, Back ou outra ação acidental.
3. Conferir BACK, START e A separados; soltar o gesto e usá-los novamente.
4. Reabrir o Guide e reiniciar a dashboard; repetir a reconexão e os botões.
5. Conferir Guide dedicado e outros índices disponíveis, quando houver hardware.

Falha manual exige continuar a correção. Resultados sintéticos, build e recibos
não constituem aceite físico nem autorização para publicar.
