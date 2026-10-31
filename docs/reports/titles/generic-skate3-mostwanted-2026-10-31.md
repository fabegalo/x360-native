# Skate 3 e Most Wanted pelo AOT genérico

Trabalho em andamento na main de 31/10/2026. Este relatório distingue os
artefatos importados, a geração de código e os testes de execução. Ainda não
há aceite de gameplay controlado nesta etapa. Most Wanted já renderizou uma
prova real, mas a validação de controles ficou pendente e houve corrupção de
heap no encerramento. A última execução observada de Skate 3 falhou na
inicialização: 0241 avançou além do acesso baixo anterior, mas encontrou uma
função indireta omitida pelo AOT. 0242 corrigiu essa omissão; a abertura seguinte
encontrou uma entrada compartilhada ausente. 0243 corrigiu essas entradas;
a abertura seguinte encontrou um switch truncado. 0244 está em preparação.

O mantenedor restringiu o trabalho a **Skate 3** e assumiu os testes visuais.
Os registros de Most Wanted abaixo são históricos. A pedido do mantenedor,
foi tentada a abertura direta de Skate 3 para seu teste manual; o agente não
enviou controles.

## Originais identificados

Os arquivos 7z fornecidos em `.local/inputs/games` contêm ISOs GDFX. A extração
pelo `x360n-extract` existente terminou com zero erros: Most Wanted tem 49
arquivos/7.123.373.273 bytes; Skate 3 tem 103 arquivos/6.404.940.920 bytes.
Arquivos 7z e ISOs foram preservados; hashes e identidades estão em
`games/<jogo>/metadata/` e nas evidências privadas.

| Executável original | Title ID | Versão | SHA-256 |
|---|---|---|---|
| Most Wanted `default.xex` | `454107D9` | `0.0.0.1` | `aebdf3c14d1e4e4ed2c130d94bc1a0e97bd6d5bf39bdfdef33c3909c9cc4ba4c` |
| Skate 3 `default.xex` | `454108E6` | `0.0.0.3` | `1db39496585c521d17a2137804f42cf73ebed2b32cac166ec42dbf772f4dcf7f` |
| Skate 3 `data/webkit/EAWebkit.xex` | `454108E6` | `0.0.0.3` | `0ee66b9558c888147f4ffdfd748cfc28950e80e46a97321da2de2afdb068523f` |

Most Wanted é a edição de 2005: o próprio XEX informa filetime
`2005-11-03 10:42:16 UTC` e importa XAM/kernel `2.0.2135.0`, mínimo
`2.0.1861.0`. Base `82000000`, entrada `8262E768`. Não foram encontrados
outros módulos XEX no disco.

Skate 3 informa PE `sk83_na_f.self`, base `82000000`, entrada `82EB4F80`,
Media ID `5C087C2C` e imports XAM/kernel `2.0.9328.0`, mínimo `2.0.8955.0`.
EAWebkit é a DLL original correspondente: PE `EAWebkit.dll`, base `88000000`,
entrada `88476190`, mesmas identidade de mídia e versões de imports. A pasta
reservada `$SystemUpdate` é inventariada, sem promover firmware a módulos do
jogo. Nenhum módulo de outra dashboard foi usado como substituto.

## Falhas reproduzidas e patch 0237

Os dois codegens originais encerraram com código 0, mas produziram traps fatais.
Isso não foi considerado resultado funcional:

- Most Wanted: branches condicionais em `824EDCF8` e `824EDD08` declaravam
  destinos desconhecidos, embora os labels `824EDD1C`/`824EDD14` estivessem
  emitidos no próprio corpo. A busca pelo dono usando a entrada mais próxima
  perde blocos fora da sequência linear quando há outra entrada intermediária.
- Skate 3: o branch em `82B0C4A4` para a função existente `82B0C658` não tinha
  CallTarget. A análise anterior havia seguido o destino como label interno;
  sua descoberta posterior como entrada não reconstruía a aresta.

0237 faz o emissor usar o FunctionNode explícito e reconcilia branches diretos
somente com entradas já existentes no grafo final. Não acrescenta hints,
funções presumidas, stubs ou bypasses. O SDK continua pinado; o patch está no
lock com hashes anteriores/finais, aplicado pelo script oficial.

Os fixtures sintéticos falham em três asserções sem a correção. A candidata
passou 28 asserções/dois casos; com os controles de gaps/CTR, 573 asserções em
18 casos. Reanálise dos dois XEX reais e da EAWebkit eliminou os traps acima.
Os avisos de pack float16_4 e função grande foram preservados e não tratados
como prova de compatibilidade. Não há alegação de ganho de performance: os
tempos de codegen foram coletados sob cargas diferentes de builds paralelos.

Um clone local do SDK pinado recebeu os 224 patches existentes naquela etapa pelo script oficial,
com os 275 hashes finais conferidos. A cópia privada preserva os originais e
um seed de cinco arquivos de usuário verificado antes/depois por SHA-256.
Renderizadores Xenos/Native não foram modificados por este trabalho.

## Importação e preparo

Importação pelo modelo oficial do launcher, com geração exclusivamente em
`.local/aot`, concluída na main:

- Most Wanted: `ge1281b20719d8d1caa6b0dd0ce82f93ba331bcc7ad1c58ffc1dfe471a7e6201`.
- Skate 3: `g195e8524b22963c0eaf69106b94c906815f5357092cc15bc8d2303ba4c55a7d`.

As tentativas iniciais de gravar o catálogo foram recusadas pelo lock de
preparações paralelas; os processos existentes foram preservados. Os imports
foram repetidos sequencialmente após a liberação. O preparo oficial nativo de
Most Wanted foi iniciado com 0237; compilação, recibos, registro My Games,
inicialização, menu e gameplay ainda estão em validação.

## Primeiros artefatos e execução limitada

Os dois jogos, a DLL EAWebkit e a dashboard 17559 concluíram o preparo oficial
em Release com 0237. O registro dos dois jogos em My Games também concluiu pelo
modelo oficial, com cópia e verificação do conteúdo no HDD. A dashboard usou
`--define PYTHON_EXECUTABLE=<caminho canônico do Python fixado>`: seu preset
representava o mesmo programa com `../..`, produzindo uma chave diferente de
SDK. Nenhum perfil ou backend gráfico foi alterado para resolver esse alias.

A primeira construção do SDK foi corretamente recusada porque o README dos
patches foi editado durante a compilação. A repetição com entradas estáveis
gerou recibo válido. A biblioteca oficial, sem substituição dos objetos da
candidata, passou as mesmas 573 asserções em 18 casos focados.

Most Wanted executou em Xenos/D3D12 por 180,208 s, encerrado pelo supervisor,
sem exceção não tratada capturada. O plugin Computer Use retornou
`app approval timed out`; não houve observação visual ou input pelo agente.
Os logs de procura por caminhos lógicos `D:\\GLOBAL`, `D:\\CARS` e outros não
comprovam ausência de originais: este disco armazena dados em arquivos BIN.
Menu e gameplay permanecem sem aceite. O clone inicial do HDD esbarrou no
limite de caminho do Windows; a cópia completa usa `.local/evidence/sg`, cujo
maior caminho registrado tinha 247 caracteres. O seed anterior foi conferido.

Durante a espera da aprovação por aplicativo, o trabalho gráfico acrescentou
seu patch `0237-share-vulkan-driver-pipeline-cache.patch` ao final do lock.
Os números dos nomes não determinam a ordem: ambos os arquivos são distintos
e a sequência continua sendo a lista do lock. O novo lock invalidou os recibos
anteriores; a segunda tentativa foi recusada antes de abrir o jogo. O patch
gráfico foi preservado. O replay oficial dos 225 patches conferiu os 275
arquivos mapeados; a inclusão explícita de `<algorithm>` no patch de codegen
alterou somente `phase_merge.cpp`, preservando os outros 274 hashes. Most
Wanted e Skate 3 concluíram novamente o preparo com essa cadeia. A biblioteca
oficial atual passou 573 asserções/18 casos (`current-regression.log`). A
dashboard também concluiu a publicação do recibo correspondente.

## Sessão de diagnóstico sem alterar preferências

`x360n session ... --diagnostic-xenos` seleciona Xenos e a API padrão da
plataforma somente para o snapshot daquela sessão. O launcher e as sessões
normais continuam seguindo a seleção global. Isso permite ensaiar a sequência
original dashboard → My Games → jogo com armazenamento isolado e sem gravar
ou restaurar temporariamente `graphics.toml`.

O teste de processo salva Native, confirma que a sessão normal recusa a falta
do plugin, percorre dashboard → jogo → dashboard com a opção explícita e
confere que as preferências continuam idênticas: oito asserções/um caso.
O recurso não altera Xenos/Native, não substitui menus originais e não constitui
evidência de gameplay.

Evidência privada: `.local/evidence/generic-skate3-mostwanted-2026-10-31/`.
O snapshot `.local/v-smw` separa fontes, originais e dados de teste; dependências
genéricas e toolchains locais podem ser compartilhados. Não há aceite de Linux,
macOS, ARM64, saves comerciais ou campanha completa.

## Execução visual e falha restante

Most Wanted apresentou a tela inicial, vídeos, menu principal 3D, Career e
Challenge Series, avisos de alias/autosave e o seletor de HDD original 17559.
Os controles foram enviados por Computer Use; os toques curtos de teclado não
foram suficientes em várias telas. Remapeamentos normais de mouse somente nos
argumentos dos processos permitiram confirmar escolhas e navegar. Sobrepor
RT ao D-pad na mesma entrada impediu uma escolha no menu; essa tentativa não
comprova defeito do jogo. Não foi observada corrida controlável. Nas primeiras
tentativas, o lançamento por My Games também não havia sido observado.
As tentativas iniciais mantiveram o limite de 180 s por child;
o processo sem exceção não foi promovido a compatibilidade funcional.

Skate 3 reproduziu leitura inválida guest `00012160` em três execuções reais,
incluindo trace e observação de escritas por debugger. A instrução original
`826B5E6C: 7F6A302A` é `ldx r27,r10,r6`; a leitura anterior em `83027DC8`
entregou zero. A tabela só recebeu escritas de zero pelo carregador antes da
falha. O corpo emitido corresponde à sequência original e já era produzido
antes de 0237; não foi encontrado um trecho omitido nesse corpo. O trace
registrou leituras bem-sucedidas de `miscboot.big`, `db.big` e `fedynamic.big`.
Isso ainda não identifica a causa anterior da tabela não inicializada e não
prova ausência de arquivos originais. Nenhum mapeamento artificial do endereço
inválido, retorno fictício ou patch do executável foi introduzido.

Evidências adicionais: `mostwanted-03` a `mostwanted-08`, `skate3-01`,
`skate3-04`, `skate3-06`, `skate-disasm.log` e `skate-host-mappings.json`.
As tentativas `skate3-02`/`03`/`05` falharam na instrumentação ou antes do
lançamento e não contam como execução funcional. O debugger privado reutilizou
o supervisor e seus gates de preparação, observando uma localização fornecida
por variável de ambiente; não alterou os bytes guest.

Na tentativa `dashboard-mostwanted-02`, com autorização expressa de até oito
minutos e limite externo de 480 s, a dashboard original mostrou os dois títulos
em Meus Jogos. O segundo cartão, `Xbox 360 454107D9`, iniciou Most Wanted pelo
handoff oficial, registrado em `20261031T151832Z-session-1-ge1281b20719d8d1c…`.
O jogo mostrou vídeos, respondeu ao Start e percorreu criação de alias,
avisos de autosave e teclado interno com o nome padrão `NAME`. A tentativa
encerrou pelo limite durante esse fluxo, antes de corrida. A origem da seleção
é a XUI original; nenhum salto artificial substituiu Meus Jogos. O hash global
de gráficos permaneceu idêntico. Os remapeamentos pelo console de diagnóstico
foram temporários: `type_text` não inseriu caracteres nesse controle ImGui,
mas eventos individuais de tecla e `Shift+minus` para `_` funcionaram.

## Carregamento de corrida: fronteiras indiretas

`mostwanted-10` percorreu Quick Race → Quick Play → briefing. Após confirmar
a corrida, encerrou em chamada indireta não registrada `82203B68`
(`LR=821E8348`). A função anterior é um tail virtual com a vtable em r11 e
o callable em r10. A descoberta aceitava somente o mesmo registrador nas duas
cargas e perdia a função imediatamente seguinte. O patch 0238 preserva o
fluxo entre r10–r12; seu teste sintético falha na base e cobre nove pares.
A biblioteca oficial passou **765 asserções/20 casos** após o preparo.

O novo executável oficial, `mostwanted-11`, passou dessa falha. Quick Play
selecionou Lap Knockout/Ocean Hills, três voltas, três oponentes e Fiat Punto.
Após a confirmação, encerrou em outra função não registrada, `8212F610`
(`LR=823C79E4`), em 382,513 s, dentro do limite de oito minutos autorizado.
O original contém seis invocadores adjacentes por descritor; o primeiro
transferia por CTR e escondia os seguintes. A candidata 0239 reconhece o fluxo
completo de callable/objeto/ajuste em campos fixos, preservando metadados e
recusando variantes ambíguas. A base falhou em duas asserções; a candidata
passou **102 asserções/um caso**, incluindo dez variantes negativas/protegidas.
Controles focados passaram **764 asserções/18 casos**. O conjunto combinado,
ligado à biblioteca oficial recompilada com 0239, passou **867 asserções/21
casos** (`production-0239-regression.log`). Preparo do título e execução com
0239 ainda pendentes neste registro. Nenhuma corrida controlável foi observada.

Os contadores resumidos do supervisor nessas tentativas não incluem o log
redirecionado pelo ensaio privado; a falha real está em `mostwanted-10/rex.log`
e `mostwanted-11/rex.log`. Contadores zero não são evidência de execução limpa.

## Verificação adicional do XEX de Skate 3

Um probe privado carregou a imagem sem executar código guest, antes dos
fixups de imports. Todos os **275 descritores de páginas** conferiram com a
cadeia SHA-1 embutida no XEX, cobrindo exatamente **18.022.400 bytes** da imagem
descomprimida (`skate-page-digests.json`). Isso verifica a consistência dos
bytes decodificados com os hashes do arquivo; não verifica a assinatura RSA
nem demonstra a procedência comercial. O valor zero inicial em `83027DC8`
também pertence à imagem conferida. O readwatch observou a primeira leitura
guest desse campo imediatamente antes da falha, sem escrita guest anterior.
A causa da inicialização ausente permanece aberta; nenhum arquivo original
necessário foi comprovadamente identificado como ausente.

## Execução com 0239 e limite do aceite

O preparo oficial de Most Wanted concluiu com 0239; a tabela AOT passou a
registrar os cinco invocadores antes ausentes em `8212F610`–`8212F690`.
Em `mostwanted-12`, o Computer Use inicialmente falhou com
`foreground window did not report a process id`. Após reinicializar a sessão
da ferramenta e redescobrir a janela, a captura mostrou um tutorial Tollbooth
e depois uma prova real com Volkswagen Golf, HUD, cronômetro, pista e colisões.
Não foi observado pelo agente o percurso de menus que iniciou essa prova.
Houve entrada concorrente na janela, confirmada pela ferramenta e por mudanças
no painel de configurações; o agente interrompeu o envio de comandos.
Assim, a movimentação observada não comprova aceleração/direção controladas
pelo ensaio. O alias `NAME` apareceu no menu; sua criação não foi validada.

Após `XamLoaderRebootToDash`, a execução encerrou em **399,907 s** com
`STATUS_HEAP_CORRUPTION` (`C0000374`), antes do limite de 480 s. O log registrou
o callback original de término e a quiescência de áudio. O frame em
`rexgpu-xenos` foi identificado por relink privado dos objetos existentes com
mapa de símbolos, sem executar/substituir o DLL: os bytes e o RVA de `.text`
conferiram exatamente com o DLL oficial. Pilha:
`D3D12Texture::~D3D12Texture+E7` → destrutor de exclusão →
`TextureCache::DestroyAllTextures+14B` →
`D3D12TextureCache::~D3D12TextureCache+1F` →
`D3D12CommandProcessor::ShutdownContext+684` → worker gráfico.
Isso localiza a detecção na liberação de texturas; não identifica a escrita
que corrompeu o heap. Renderizadores não foram alterados. Evidências:
`mostwanted-12/{rex.log,supervisor.log,result.json}` e
`xenos-shutdown-symbols.json`. Não há aceite de gameplay controlado ou de
retorno limpo à dashboard nesta execução.

## Skate 3 com o SDK atual

Skate 3 e EAWebkit concluíram novamente o preparo oficial com 0239.
`skate3-08` terminou em corrupção de heap durante a apresentação D3D12, antes
do código guest de inicialização; a pilha passou por `nvwgf2umx` e
`D3D12Presenter::PaintAndPresentImpl`. A tentativa `skate3-09` alterou somente
o cache para uma pasta privada nova e voltou à leitura inválida guest
`00012160`, em **43,252 s**. Uma observação por condição não comprova que o
cache causou a corrupção anterior; os caches existentes foram preservados.

O watchpoint de `skate3-09` observou a escrita guest `00000010` em `83072268`
pelo último inicializador da tabela C++, `82F6C750` (RVA host `0044C4B6`,
`sub_82F6C750+A6`). Portanto, a hipótese de pular a tabela inteira por falha de
inicialização do CRT não explica o ponteiro nulo. A arena observada foi
`0000000100000000`, conferida antes de interpretar o endereço do watchpoint.
O novo executável continua falhando no mesmo corpo `826B57D0+210B` host;
não chegou ao menu. Uma inspeção dos 2.960 inicializadores emitidos não
encontrou perda evidente da restauração de r30/r31; isso não substitui uma
verificação completa de semântica PPC. Os cinco arquivos existentes no HDD de
origem antes deste trabalho continuam idênticos por SHA-256 após esses ensaios.

## Correção das instruções de MSR — 0240

A inicialização de Skate 3 usa `mfmsr`/`mtmsrd L=1`, inclusive no wrapper
`826B71E8` que chama o corpo com a falha registrada. O codegen usava a máscara
`8020`, atualizando IR e descartando RI. Conforme o
[PowerPC Book III v2.01, página 19](https://kib.kiev.ua/x86docs/POWER/ISA/es-archpub3.pdf),
essa forma deve atualizar somente EE e RI (`8002`). `mfmsr` também devolvia
somente EE do lock, descartando os demais bits modelados em `PPCContext::msr`.
0240 corrige as duas traduções, preservando o caminho L=0 e o mecanismo de lock.

O teste sintético gera C++ pelo AOT, compila esse código e executa as
instruções reais emitidas, sem originais, janela, renderer ou memória guest.
A base falhou ao escrever RI; a correção apenas da escrita ainda falhou ao
ler RI. A candidata completa passou **80 verificações de estado/leitura**
com vinte pares de valores, incluindo IR e bits não envolvidos na escrita.
O lock é liberado antes de avaliar cada resultado, inclusive nas falhas.
Evidências: `msr-red.log`, `msr-readback-red.log` e `msr-final-green.log`.
O alvo `x360n_msr_native` e o CTest `sdkcheck.msr-native` preservam a regressão.
O CTest focado contra a biblioteca oficial também passou nas 80 verificações
(`msr-production-test.log`, 0,15 s para o teste, sem medição de performance de
jogo). O preparo oficial de Skate 3 e EAWebkit com 0240 concluiu com saída zero
em 1.378,678 s; o recibo foi emitido. O wrapper `826B71E8` recém-gerado contém
quatro escritas com `8002` e duas leituras preservando os bits modelados,
conferidas em `skate-msr-emission-proof.json`.
Esse erro de instrução está comprovado; não foi demonstrado que explique o
acesso `00012160`, e não há novo aceite de inicialização/menu/gameplay.

## Referências de memória e hipóteses restantes

O [fork público usado por Skate3Recomp](https://github.com/mchughalex/rexglue-skate3/commit/1081b0ea273e88928802bb9799a5ae1750aaf3f0)
compromete diretamente uma página host em `10000` para tolerar leituras por
base nula com deslocamento, sem registrar a alocação nos metadados guest.
Esse contorno coincide com o endereço da falha, mas não demonstra o contrato
do console e **não foi incorporado**.

Há também uma diferença concreta de política: o
[Xenia de referência](https://github.com/xenia-project/xenia/blob/master/src/xenia/memory.cc)
aloca objetos HLE pelo heap virtual comum, enquanto o SDK pinado usa uma faixa
separada em `30000000`, introduzida no commit
`78ed28339c7dc5dfc96b95d1816a5410b238c24f`. Assim, não se pode presumir que a
mesma página baixa tenha o mesmo estado nos dois runtimes. Isso ainda não
estabelece o layout original do console nem autoriza mudar a alocação apenas
para tornar o acesso legível por meio de uma página artificial.

## Alocação compartilhada dos objetos HLE — 0241

0241 adota a busca nas lacunas baixas usada pelo
[Xenia consultado em revisão fixa](https://github.com/xenia-project/xenia/blob/95a5c3ee250f80c3b9d139658649d9ffb6db3eec/src/xenia/memory.cc):
`BaseHeap::AllocSystemHeap` começa a busca nas lacunas comuns, mantendo seu
limite superior completo, inclusive a reserva existente para objetos do kernel.
`Memory::SystemHeapAlloc`, os limites ordinários/de stack, metadados,
reserva/commit, proteções, alinhamento, zeragem, liberação e delegação física
permanecem iguais.
Não usa Title ID, endereço do acesso, pré-alocação extra nem retorno fictício.
O layout interno preciso do console continua não comprovado; a política tem
uma referência de implementação identificada, não um dump do console.

Os testes comparativos usaram o mesmo executável sintético com o DLL oficial
e um DLL privado relinkado dos mesmos objetos, substituindo somente
`xmemory.cpp` pela candidata. A base falhou em três verificações da política
de posicionamento; imports, dispatch e demais controles passaram. A candidata
passou **357 asserções/14 casos**, incluindo alocações normais e de sistema,
reutilização de lacunas, zeragem, estado/proteção, contagem de páginas livres,
proteção da página zero preservada, caminho físico e capacidade do kernel após
esgotar a faixa ordinária. A primeira candidata, que trocava a chamada de
alocação, passou os controles iniciais mas falhou neste último cenário e foi
rejeitada antes de produzir um novo recibo do título. A implementação final
ajusta somente o limite inferior da busca do alocador de sistema. Nenhum código
original foi executado. Os DLLs e executáveis oficiais existentes não foram substituídos
pelo ensaio privado.

Um segundo probe executou apenas `Runtime::Setup` em tool mode, com os
inicializadores HLE reais e diretórios privados, sem GPU, input, áudio ou XEX.
Na base, `12160` tinha estado livre, sem proteção. Na candidata, a região
`12000`, de 4 KiB, estava reservada/comprometida RW por uma alocação HLE real;
a leitura de oito bytes resultou zero. Portanto, a diferença do mapa existe
antes do código do jogo. Não foi preciso comprometer uma página avulsa nem
ocultar a consulta do heap. Isso permite testar a correção no título, mas
**não comprova menu ou gameplay**.

Evidências iniciais: `system_heap_contracts-{base,candidate}.log`,
`system_heap_probe-{base,candidate}.log` e `system-heap-contract-results.json`.
Versão final: `system-heap-runtime-final/test.log` e
`system-heap-final-probe-isolated.log`; rejeição da primeira candidata:
`system-heap-reserve-first-candidate.log`.

O replay oficial final contém **229 patches/276 arquivos**, com todos os hashes
conferidos após a compilação. O mesmo conjunto de **357 asserções/14 casos**
passou contra o novo SDK oficial, sem objetos privados substituídos. O probe
oficial repetiu a alocação HLE válida e leitura zero em `12160`, sem código de
jogo. Evidências: `system_heap_contracts-production.log`,
`system_heap_probe-production.log` e `system-heap-production-result.json`.

O preparo oficial de Skate 3 e EAWebkit concluiu com saída zero em
**1.313,028 s**, sem timeout; o recibo foi emitido e o marcador de preparo
incompleto removido. A DLL ao lado do jogo confere com a biblioteca oficial
testada, SHA-256
`edd362705b897bc633b778dfea1ca517231a9c7c154921d0eeaea5e3f5915011`.
Logs: `skate3-prepare-0241-refined.log` e seu `-result.json`.
Isso conclui importação/AOT/compilação desta revisão, não o aceite de
inicialização, menu ou gameplay do jogo. A execução posterior é registrada
abaixo, sem promover esse resultado a compatibilidade funcional.

O preparo adicional da dashboard foi interrompido na configuração por pedido
do mantenedor. O trabalho voltou exclusivamente ao Skate 3; não houve nova
sessão de jogo ou Computer Use.

## Abertura solicitada e callback tardio — 0242

A abertura direta para o teste manual do mantenedor usou o supervisor oficial,
HDD isolado e argumentos Xenos/D3D12, preservando a preferência gráfica global.
Não houve navegação ou envio de controles pelo agente. A build 0241 encerrou
após **14,250 s**, ao despachar o destino não registrado `82CDE398`, com LR
`8258C9B8`. O log avançou além do acesso `12160` e alcançou inicialização de
GPU/áudio, mas não comprova menu ou gameplay. Nenhum stub foi contado.
Evidência: `runs/20261031T191356Z-skate3-user-0241-20261031T191353Z`.

Os bytes originais mostram que esse callback é materializado por `lis/addi`
num construtor e armazenado num objeto. O construtor é descoberto pelo gap fill
depois da busca inicial por referências; o callback, situado após um `bctr`,
continuava absorvido pela propriedade especulativa do gap fill. 0242 repete a
busca limitada após descobrir esses blocos e permite que uma referência válida
promova essa propriedade especulativa. Preserva as restrições de metadados,
configuração, tabelas de salto, imports/exports, registradores e término do
prefixo. Não contém identidade ou endereço do título nem fallback de runtime.

A regressão sintética falha na base em duas verificações: entrada e registro
gerado do callback. A candidata passa **60 asserções/um caso**, com seis
variantes, incluindo clobber de registrador, predecessor condicional e proteção
de metadados/configuração. Os controles focados de gap/CTR/emissão passam
**867 asserções/21 casos**. O codegen privado do XEX original concluiu em
**41,848 s**, emitindo a função e o registro de `82CDE398` e callbacks vizinhos.
Esse ensaio não substituiu fontes geradas ou binários oficiais. A comparação
do registro completo passou de 45.807 para 45.808 entradas, acrescentando apenas
o callback ausente e sem remover entradas.
Evidências: `late-ref-final-{base,candidate}.log`, `late-ref-controls.log`,
`late-ref-skate-codegen-result.json` e `late-ref-generated/`.

O replay oficial de 0242 foi aplicado pelo script do repositório. Os mesmos
testes de descoberta/emissão, agora ligados exclusivamente ao novo SDK oficial,
passam **927 asserções/22 casos**, sem objetos candidatos substituídos.
Evidência: `0242-production-test-result.json` e `0242-production-test.log`.
A preparação oficial de Skate 3/EAWebkit terminou com saída zero em
**1.087,790 s**, sem timeout e com recibo final. A nova abertura solicitada
para teste manual passou da omissão anterior, mas encerrou em **30,608 s** ao
despachar `8293ABE0`, LR `82A57560`. Evidência:
`runs/20261031T194203Z-skate3-user-0242-20261031T194200Z`. O agente não enviou
controles. Menu e gameplay continuam sem aceite; não houve mudança de renderer.

## Entradas compartilhadas referenciadas — 0243

Os bytes originais materializam `8293ABE0` por `lis/addi` e armazenam o endereço
num registro de callbacks. Uma outra entrada, `8293ABD8`, adapta um argumento e
salta diretamente para esse mesmo bloco. O corpo compartilhado ultrapassa o
prefixo linear curto aceito pela busca de referências. Além disso, o wrapper
já foi descoberto como callable; sua propriedade é inferida, sem extensão
original PDATA para esse intervalo.

A correção permite registrar uma entrada referenciada que coincide com um
bloco CFG já descoberto e é precedida por um branch direto incondicional para
esse exato bloco. Serve para proprietários GAP_FILL ou DISCOVERED, preservando
metadados/configuração, helpers, proprietários de vtable, imports/exports e
tabelas de salto. Não aumenta indiscriminadamente o limite do prefixo e não
contém identidade ou endereço de título. O wrapper mantém sua entrada e a
adaptação do argumento; o merge registra a transferência para o shared tail.

Uma primeira candidata, limitada a GAP_FILL, passou sua fixture inicial mas
não registrou o destino do XEX original. Foi rejeitada antes do replay oficial.
A regressão foi ampliada para wrappers previamente descobertos, corpos longos
e curtos, registro e chamada AOT emitida, destinos/predecessores inválidos,
clobber e extensões declaradas. A base falha; a candidata completa passa
**108 asserções/um caso**, em dez variantes, e **1.035 asserções/23 casos** no
conjunto focado de descoberta/emissão. O novo codegen do XEX registra o destino
reproduzido e outras quatro entradas compartilhadas equivalentes, sem remover
registros: 45.808 → 45.813 entradas na comparação privada.

Evidências: `shared-tail-final-results.json`, `shared_tail_*-final.log`,
`shared-tail-skate-codegen-v2.log` e `0243-original-codegen-comparison.json`.
O replay oficial foi aplicado pelo script do repositório. O conjunto de
**1.035 asserções/23 casos** também passou ligado exclusivamente ao novo SDK
oficial, sem substituir objetos privados; evidência em
`0243-production-test-result.json`. A preparação oficial terminou com saída
zero em **1.048,647 s**, sem timeout, emitindo o recibo. Foram conferidos
231 patches/276 arquivos, sem divergências, e todos os seis destinos das duas
omissões anteriores estão registrados. Artefatos, hashes e dados originais:
`skate-0243-final-artifacts.json`.

A abertura seguinte exibiu uma janela preta, conferida por Computer Use. O
agente apenas ativou/capturou a janela, sem enviar controles. O processo encerrou
em **34,149 s**, com `C000001D`, sem aceite de menu/gameplay. Evidência:
`runs/20261031T200816Z-skate3-user-0243-20261031T200813Z`. Os renderizadores
permaneceram fora do incremento. O log também registra o aviso preexistente
`IoDismountVolumeByFileHandle - stub`; não se declarou esse import funcional.

## Limite de switch com default separado — 0244

O trap foi mapeado pela tabela de unwind/mapeamentos do PE para
`823D7368`, em `__builtin_trap(); // Switch case out of range`.
O XEX original compara o índice sem sinal com 642 e faz
`ble dispatch; b default`. A busca de limites parava no branch incondicional
do default, perdendo o compare; sem limite reconhecido, usava a heurística de
512 slots. O código emitido omitira os casos 512–642 de uma tabela de 643
entradas. O trap não foi eliminado nem substituído por sucesso/fallback.

0244 reconhece essa forma de guarda: verifica CR/condição, destino da entrada,
saída do default, ausência de bypass no prefixo e preservação do índice no
dispatch. O count sem sinal provado dirige a leitura da tabela; a heurística
de 512 permanece igual quando essa prova falha. A nova regra não usa título,
endereço ou tamanho específico de tabela.

A base sintética falha; a candidata passa **101 asserções/um caso**, com oito
controles negativos, e **1.136 asserções/24 casos** de descoberta/emissão.
A fixture compilada pelo AOT real executou **todos os 65.536 índices de
16 bits**, incluindo os casos além de 511 e o default, conferindo retorno,
CTR, LR e um GPR não volátil. Usa só um XEX sintético e runtime em tool mode,
sem GPU, input, áudio ou conteúdo original. O novo codegen do XEX original
também emite os 643 casos, sem novos traps nessa função além do default
inalcançável por índices admitidos pela guarda.

Evidências: `split-default-final-{base,candidate,controls}.log`,
`split-default-native-result.json`, `split-default-native-test.log` e
`0244-original-switch-proof.json`. O replay oficial foi aplicado. O novo SDK
oficial também passou **1.136 asserções/24 casos** e os **65.536 índices** na
fixture gerada e compilada exclusivamente com suas bibliotecas, sem substituir
objetos privados; evidências em `0244-production-test-result.json` e
`0244-production-native-test.log`. A preparação oficial de Skate 3/EAWebkit
terminou com saída zero em **1.060,677 s**, sem timeout, emitindo o recibo.
232 patches/276 arquivos conferidos sem divergências; a saída oficial contém
os seis destinos das falhas indiretas e os 643 casos do switch reproduzido.
Hashes, recibo e estado dos dados originais estão em
`skate-0244-final-artifacts.json`. A abertura para teste manual foi iniciada;
menu/gameplay continuam pendentes.

## Relato de travamento e repetição de 0244

Na sessão manual `20261031T203803Z-skate3-user-0244-20261031T203800Z`, o
mantenedor relatou aparente travamento. O processo terminou em **233,517 s**
com `C0000374` (heap corruption), detectado em `RtlFreeHeap`, chamado da
thread `TimerQueue::TimerThreadMain`. Essa pilha identifica a detecção, não
quem escreveu incorretamente na memória. A corrupção e o encerramento seguem
pendentes; não foi aplicado um patch especulativo à fila de timers ou renderer.

Foi criado apenas um supervisor diagnóstico privado, sem substituir o oficial
ou qualquer DLL do jogo. Captura pilhas/registradores com o debug API aos
60/100/140 s e retoma as threads, sem entradas de UI ou alterações em memória
guest. Um processo sintético validou três capturas e saída normal antes da
reprodução. Evidências: `snapshot-probe-result.json` e `child-snapshot.cpp`.

A repetição `skate3-hang-snapshot-01` usou a mesma build, HDD isolado e cache
da sessão anterior, com limite de 180 s. Não capturou exceção não tratada;
encerrou por **timeout deliberado**, não por crash. O supervisor não estava
configurado para analisar o log ReXGlue externo: seus contadores zero de stubs
e erros não representam ausência desses registros no `rex.log` privado.
Nas três amostras, a thread principal de trabalho acumulou CPU e apareceu em
funções PPC diferentes; a thread de comandos gráficos também progrediu.
Isso não demonstra fluidez nem identifica a causa da corrupção anterior.
Evidências: `skate3-hang-snapshot-01/{result.json,stacks.txt,rex.log}` e
`hang-snapshot-analysis.json`.

Durante essa repetição, o mantenedor informou: **"agora nao deu problema
entreou normal no jogo"**. Registra-se a entrada no jogo relatada pelo usuário,
sem inferir validação completa de controles, saves, estabilidade ou encerramento.
Após o timeout, a sessão manual foi reaberta pelo supervisor oficial, sem
limite de tempo, com o mesmo cache/HDD isolado. Uma reabertura intermediária
que selecionara um cache novo por erro no comando foi encerrada apenas pelo
PID/identificador verificados e reiniciada com o caminho correto. Sessão final:
`skate3-user-0244-20261031T205509Z`. Nenhuma correção de produto foi acrescentada
nesta repetição e nenhum renderer foi alterado.

## Pausas ocasionais em menus/carregamentos

O mantenedor relatou congeladas ocasionais, localizadas em **menus e
carregamentos**. A sessão manual final permaneceu aberta. Uma coleta externa
de 20 amostras, entre 20:57:41 e 20:58:04 UTC, registrou 42,056 MiB de I/O de
leitura em 22,749 s, com rajadas de até 13,172 MiB por intervalo, e 67,844 s
de CPU agregada. A thread de trabalho continuou executando em todos os
intervalos. São contadores do processo, não uma medida de disco físico ou FPS.
Evidências: `menu-loading-cpu-io-{01,summary-01}.json`.

Um sampler privado usou a API documentada de
[process snapshots da Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/processsnapshot/nf-processsnapshot-psscapturesnapshot)
para capturar somente contextos de threads, sem clonar memória, reiniciar o
jogo ou enviar entradas. A validação no próprio processo e a coleta ao vivo
conferiram captura/liberação. As 20 amostras ao vivo custaram 1,875–2,731 ms
por captura, abaixo do limite de 5 ms que interromperia a coleta.
A thread principal de trabalho acumulou 18,250 s de CPU no intervalo e apareceu
em `82B4D8C0` em nove amostras e `82B4CE00` em duas. A primeira rotina contém
espera com hints PPC e consulta de estado; os endereços foram associados pelo
unwind/mapeamentos do próprio PE, sem símbolos de título inventados.
Evidências: `thread-sampler.cpp`, `menu-loading-pss-01.txt` e
`menu-loading-pss-summary-01.json`.

As amostras não foram marcadas com o instante exato de cada congelada.
Portanto, não provam se a pausa vem de I/O, compilação gráfica, sincronização
ou outro custo. Não foi atribuído um ganho de FPS, aplicado um contorno por
título, nem alterado um renderer. Entrada no jogo relatada pelo mantenedor
permanece um resultado distinto de estabilidade/performance aprovadas.

## Isolamento do incremento para commit

O checkout usado nos preparos continha também um patch Vulkan de trabalho
paralelo, `0237-share-vulkan-driver-pipeline-cache.patch`. Esse patch e as
alterações em `src/native`/`tests/native` não fazem parte deste incremento.
O lock isolado contém os oito patches próprios, 0237 de emissão e 0238–0244,
com os hashes Vulkan anteriores preservados. A reaplicação independente de
**231 patches** sobre o SDK fixado e seus submódulos conferiu **276 arquivos**,
sem divergências. Evidência privada: `commit-isolated-replay-result.json`.
O checkout de trabalho e o jogo aberto foram preservados; não houve repetição
de sessões de jogo ou suítes para realizar o commit.
