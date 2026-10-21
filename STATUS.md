# STATUS

Estado consolidado em **2026-10-21**; a referência funcional integrada histórica é `1844245`.
Este arquivo resume o estado atual; os relatórios vinculados preservam datas,
falhas iniciais, hashes, comandos e limites de cada validação. Resultados de uma
etapa histórica não certificam automaticamente o build atual nem outros títulos.

Launcher (21/10): corrigido o fluxo que anunciava jogo pronto pelo recibo mas
iniciava a dashboard sem concluir o registro na My Games. Preparar/iniciar agora
concluem o registro pelo aplicativo, com validação independente no supervisor.
Build Release inicial e 223 asserções focadas passaram; a 17559 abriu pelo botão
normal. O mantenedor confirmou dashboard, My Games e Forza funcionando.
Launcher/supervisor finais recompilados em Release após encerramento dos processos.
Tela cheia configurável no aplicativo agora dimensiona a janela da sessão;
79 asserções do modelo e 63.918 asserções de 15 casos da interface passaram.
Reabertura agora reaproveita recibos atuais para mostrar pronto/iniciar, sem
acrescentar hashes de inventários à listagem; 113 asserções/10 casos passaram.
Validação visual da tela cheia pendente. Captura de pilha opcional disponível
em Diagnóstico, desligada normalmente. A criação de perfil
17559 caiu após criação/login bem-sucedidos, com leitura guest de `0x00000005`;
pilha não capturada e causa ainda em diagnóstico. Forza também caiu após 302.052 ms,
por leitura guest `0x02A2C6F8`, sem pilha. Ambas as falhas continuam sem correção
certificada. Aceite de conquistas separado;
commit local autorizado pelo mantenedor em 21/10; push não solicitado nesta rodada.
[Correção, evidências e pendências](docs/reports/launcher-registration-flow-2026-10-21.md).

START no jogo (21/10): reproduzida em teste a perda de toques liberados antes
do fim da janela do Guide. Patch 0112 preserva o toque para a consulta de estado
e avança o pacote visível quando a filtragem muda os botões. 844 asserções
focadas de entrada/Guide passaram em Release; confirmação com controle físico
pendente. SDK continua pinado, sem hacks por título.
[Evidências e aceite](docs/reports/start-short-tap-2026-10-21.md).

Guide durante boot (21/10): o mantenedor confirmou que as duas dashboards
bloqueiam o Guide na intro. A pedido dele, o código da 17559 foi alinhado ao
mecanismo existente da 1888: `BootIntroRunning()` controla a entrada do callback
de swap, com estado pertencente ao bootstrap original. Removidos o descarte
explícito de apertos, a guarda adicional de solicitações e o teste que exercitava
essa política anterior. O plugin de UI dos jogos não executa a intro de console.
A 1888 não foi alterada. A compilação Release do alinhamento foi concluída na
validação para commit, após o bloqueio inicial por ambiente em uso. Evidência:
`.local/evidence/validation/precommit-20261021/build-dash.log`.

Conquistas — cor/transparência (21/10): o mantenedor confirmou o aviso original
“10G — Bem-Vindo Ao Horizon”, mas com fundo ciano e tonalidade vermelha.
Patch 0115 corrige a leitura BGRA sem byte swap da superfície D1OVL. Release:
422 asserções/oito casos e 64 pixels na GPU passaram; o controle negativo com
o decodificador anterior falhou como esperado. Confirmação visual das cores
e transparência no gameplay ainda pendente. Builds Release da 17559 e do Forza
concluídos, com recibos e 18 hashes de artefatos finais conferidos. A pilha de
114 patches foi reproduzida, com 124 hashes finais idênticos. Commit solicitado pelo mantenedor;
sem autorização nova de push.
[Diagnóstico e validação](docs/reports/achievement-overlay-2026-10-21.md).

Conquistas — reteste anterior sem visual (21/10): a sessão Forza
`20261021T125515Z-session-3-forza_horizon` confirmou novamente fila e recursos
originais carregados, mas o mantenedor não viu o aviso. Identificado o caminho
de scanout D1OVL emitido pelo compositor original e ausente no backend. Patch
0114 implementa sua composição no D3D12, preserva estado do jogo e desliga a
camada no descritor vazio. Release: 416 asserções/oito casos; 48 pixels de
GPU sem janela; shader reproduzido; 113 patches reaplicados/124 hashes conferidos.
Builds Release da 17559 e do Forza concluídos; recibos e hashes dos 18 artefatos
finais conferidos, sem preparação incompleta.
Aceite da notificação original em gameplay continua pendente.
[Causa, correção e validação](docs/reports/achievement-overlay-2026-10-21.md).

Conquistas — notificação 17559, rodada anterior (21/10): a sessão real do mantenedor gravou a
conquista 155 do Forza, mas o aviso original falhou com `8007000E`. Reproduzido
em sonda sem janela: apresentador válido, fila sem tarefa XAM corrente. Patch
0113 fornece contexto síncrono do host; a mesma fila original retorna sucesso
com ele. 37 asserções focadas de tarefas passaram em Release. Builds finais da
17559 e do Forza concluídos, com recibos novos; a fila foi validada com ambos os
runtimes finais. No reteste do mantenedor houve som, mas nenhum visual; o log
confirma a cena original carregada. Encontrada inicialização ausente de dois
campos exigidos pelo compositor original de popups; corrigida no host 17559.
Sonda otimizada dos desvios originais: seis casos passaram, sem GPU. Build
Release concluído, recibo novo e hashes dos artefatos conferidos; exibição visual
e aceite continuam pendentes. Desbloqueio existente
preservado; nenhum jogo executado pelo agente.
[Evidências e aceite](docs/reports/achievement-notification-2026-10-21.md).

Conquistas offline — implementação 17559 (21/10): serviços XGI/XUser/XAM,
metadados originais XDBF, persistência por XUID/título/conquista, Gamerscore e
contadores integrados, enumeração pelo Title ID solicitado e ponte para o
apresentador original de notificações. SDK permanece pinado com patches
0106–0111 reproduzíveis. O 0111 impede que ferramentas AOT publiquem cache de
conquistas na pasta original de entrada. Testes técnicos cobrem erros, duplicação, operações
assíncronas, isolamento e reinício; os recursos locais de Forza, Castle e
Motocross foram lidos sem executar gameplay. **Aceite funcional pendente**:
o mantenedor precisa obter uma conquista real, observar a notificação/Guide e
conferir a persistência após reiniciar. Nenhum jogo/dashboard foi executado pelo
agente, nenhum save ou perfil existente foi editado e a Retail 1888 ficou fora
do escopo. O mantenedor autorizou validar e fazer commit local em 21/10;
isso não representa aceite funcional da notificação nem autorização de push.
[Implementação, validação e aceite manual](docs/reports/offline-achievements-2026-10-21.md).

Entrada P0 (19/10): reproduzido acesso inválido `0xC0000005` na enumeração
concorrente do InputSystem. Patches 0103–0105 serializam hotplug, retiram callbacks
SDL com segurança e vinculam o gesto Guide à conexão e às transições da UI.
START recebido pelos originais também significa Confirmar: captura antes de
carregar o Guide e no fechamento impede a entrega tardia do gesto. As dashboards
voltam a usar a janela de 150 ms nos quatro usuários, preservando configuração
explícita; SDK permanece opt-in. Testes técnicos não substituem o aceite físico,
que continua pendente. Passaram 935 asserções de entrada/UI, sondas de hotplug e
Guide original 1888, isolamento e recibos Release novos das duas dashboards.
Na rodada visual posterior autorizada, a 1888 completou dois ciclos integrados
do Guide sem entregar os 24 keystrokes do gesto à XUI; na 17559 foram observados
abertura, fechamento por B e nova abertura após reiniciar. As sessões saíram
com código 0 e sem exceção capturada. Testes focados e hashes Release foram
reconferidos antes do envio remoto solicitado; isso não substitui o aceite físico.
O encerramento observado na 17559 e o crash de
createprofile na 1888 têm evidências distintas.
[Diagnóstico, validação e roteiro manual](docs/reports/controller-reconnect-guide-2026-10-19.md).

Biblioteca do launcher (19/10): removida a associação automática entre receitas
de compatibilidade/builds locais e jogos adicionados. Todos os jogos passam pelo
mesmo registro de importação e remoção; Castle/Motocross/Forza não são itens fixos.
Após a limpeza manual do catálogo, a 17559 passou pela conferência somente de
leitura com biblioteca vazia e zero rotas de jogos. Frontend Release do atalho
atualizado para 0.56.8-dev; candidato 0.56.8 em build limpo e ZIP extraído
aprovados. Nenhuma execução de dashboard/jogo pelo agente. Nos testes manuais
do mantenedor, a 17559 iniciou e saiu com código 0; a 1888 iniciou, mas registrou
uma chamada não resolvida após Guide/criação de perfil, pendente de diagnóstico.
[Correção e evidências](docs/reports/library-membership-2026-10-19.md).

Correções da revisão externa (19/10): estado de entrada vinculado ao backend e
à conexão (0102); janela BACK/START opt-in por usuário, desligada por padrão;
0095 usa chaves legadas locais, sem valores no payload; atualizador com diário
persistente, recuperação independente e canais estável/preview separados.
Regressões Release novas aprovadas, incluindo 24.576 verificações dos aliases CR
regenerados e 1.263 asserções do atualizador. 1888/17559/Motocross, Castle, Forza,
Charlie e o AOT 4D530A81 receberam recibos Release novos com todos os hashes
conferidos; frontend do atalho recompilado. Candidato local 0.56.6, build limpo,
gates de conteúdo/licenças e ZIP extraído aprovados. Aceite manual da 1888/17559
pendente; sem push/publicação. Nenhuma dashboard/jogo foi executada nesta tarefa.
[Evidências e pendências](docs/reports/review-fixes-2026-10-19.md).

Dashboards 1888/17559 — revisão pós-commit anterior (19/10): corrigidos LB/RB trocados no
0101 e o START do atalho BACK+START, que chegava à dashboard antes do GUIDE em
controles sem botão Guide (`guide_chord_ms`; o default de 150 ms daquela etapa
foi retirado pelo 0102).
Também corrigidos firmware ≠ 1/5 (fecha a plataforma, sem fatal), banner sem
escrita, `crset`/`crclr` e `apply-patch` alinhado ao loader. Testes focados
passaram; aceite com o controle físico pendente.
[Revisão](docs/reports/dashboard-1888-guide-2026-10-19.md#revisão-pós-commit-bf1c1a9--correções-e-atalho-backstart).

Retail 1888 — Guide (19/10): corrigida a chamada de fechamento indevida no loop
de frames do host original. Após reprovação manual da reabertura, o patch 0101
passou a reter somente os controles do gesto de abertura/fechamento até seu UP;
`guide_input.h` e seu descarte geral continuam removidos. Evidências anteriores:
269 asserções/10 casos de entrada, 51 asserções dos adaptadores e ensaio CPU com
timers reais aprovados. A revisão local posterior corrigiu duas saídas de erro
que deixavam o recurso registrado após descarregar a aplicação e restringiu o
novo teste CNG ao Windows. Revisão sem build, UI ou nova execução de testes
nativos, conforme pedido do mantenedor, que autorizou o commit. Não há
recertificação dos binários após esses ajustes. Pendem aceite manual da
reabertura/controle, relógio/AM–PM, erros XMP e serviços completos.
[Correção, revisão e limites](docs/reports/dashboard-1888-guide-2026-10-19.md).

Retail 1888 (18/10): perfil próprio Release com 22 originais locais identificados
e SHA-256, snapshot reproduzível e isolamento por versão. A interface Blades
original renderizou Games/Media/System e Console Settings; navegação por teclado
observada e controle físico confirmado pelo mantenedor. Selecionável no launcher,
sem biblioteca de jogos nesta etapa. Intro original habilitada: animação/logo
renderizados, som confirmado pelo mantenedor e retorno original à Blades em
11,867 s. Build Release/recibo com bootanim próprio e 200 asserções de isolamento
aprovados. [Intro: contrato, execução e limites](docs/reports/dashboard-1888-intro-2026-10-18.md).
Os quatro smokes iniciais limitados terminaram por timeout,
sem crash capturado; Guide/perfis/serviços/jogos sem aceite. Recurso localizado
`pt-br/shrdres.xzp` ausente; XAM original inventariado, sem execução AOT certificada.
Sem avanço para outra versão. [Hashes, testes e limites da 1888](docs/reports/dashboard-1888-2026-10-18.md).

Ambiente local Release (18/10): launcher, supervisor e extrator reconstruídos
em Release na árvore usada pelo atalho; startup agora exige Release. Seis
produtos nativos foram compilados nesse modo. O mantenedor reduziu o aceite
final a 17559 e Castle: ambos foram recertificados e executaram por 30 s,
com 1.620 swaps e zero falhas de swap cada; 1.161 arquivos originais de usuário
auditados permaneceram intactos. Preflight do smoke: Castle 5,498 s; 17559
13,354 s, sem novo comparativo equivalente de latência. Charlie/Minecraft
mantêm recibos Release; Motocross foi interrompido na recertificação e Forza
ainda precisa recertificar os metadados de DLL Release. A 6717 foi bloqueada
pelo gate de `XenonJKLatin.xtt` sem identidade verificada; exe/recibo antigos
arquivados, sem fallback. [Resultados e limites da migração local](docs/reports/local-release-migration-2026-10-18.md).

Configuração de produção (18/10): o pacote desktop já usava Release, mas os
presets padrão e a geração AOT usavam RelWithDebInfo. Corrigidos para Release;
o driver impede reuso/reconfiguração omitida de outro modo e confere flags antes
de certificar títulos/SDK/DLLs. Distribuição fora de Release é recusada pelo CMake;
gates local/ZIP também recusam evidência Debug, mesmo com metadados Release.
Na auditoria inicial, frontend Release foi compilado em árvore separada e suas
flags conferidas; jogos antigos ainda não haviam sido reconstruídos/executados.
Métricas anteriores do
frontend Debug não representam o pacote final. [Validação e limites](docs/reports/release-configuration-2026-10-18.md).

Preflight de lançamento (18/10, sobre `99a7764`): comparação alternada no mesmo
acervo reduziu a média da 17559 de 31,27 para 19,35 s (38,13%); Charlie 9,27 →
7,74 s (16,47%); Moto 10,91 → 7,98 s (26,83%); Castle 8,12 → 6,88 s (15,32%).
Read-ahead limitado a quatro workers, inventários sem enumerações repetidas dentro
da chamada e dois fontes do verificador otimizados em Debug, mantendo asserts.
Sem cache por timestamp/aprovação persistente ou mudanças no launcher, catálogo,
renderer e SDK. 37 regressões focadas/32.505 asserções passaram. Os quatro smokes
autorizados foram executados: 17559/Moto/Castle registraram swaps e encerraram
pelo prazo de 30 s; Charlie voltou à dashboard após bootstrap NetCF, sem swaps.
Os 1.132 arquivos de usuário originais auditados foram preservados. O acervo da
17559 cresceu; não comparar diretamente com os 21,87 s históricos. Tempos por
etapa, I/O, aumento de memória/leituras especulativas, tentativa descartada e
limites de primeiro frame: [relatório de preflight](docs/reports/startup-preflight-2026-10-18.md).

Preparação e lançamento (18/10, sobre `c84a4b4`): reconstrução nativa completa
17559 703,12 → 552,65 s (21,40%); no-ops dos quatro alvos 31,69–32,96% mais
rápidos, com zero CMake/codegen/objetos. A validação anterior ao processo caiu
28,79 → 21,87 s na 17559; Charlie 10,63 → 9,30 s; Moto 12,25 → 10,75 s;
Castle 8,68 → 8,15 s. SHA integral e isolamento preservados; transações novas
a cada sessão, sem aprovação persistente. Abertura/interface do launcher mantidas.
Não foram executados jogos; startup real e primeiro frame dependem de teste manual.
Memória adicional da transação e resultados sem ganho estão documentados no
[relatório de preparação/startup](docs/reports/preparation-startup-2026-10-18.md).
Complemento: corrigidos objetos AOT genéricos acima de MAX_PATH e a invalidação
incondicional de projetos idênticos. Charlie/Moto recompilaram somente um objeto
host por alteração, sem PCH ou link de DLL; 34/5 DLLs preservaram SHA-256. A
geração idempotente real de Charlie levou 4,37 s e manteve seu recibo válido.
O incremental estável de Charlie permaneceu em aproximadamente 123 s: não houve
ganho de parede nesse ensaio; a redução de trabalho não é vendida como latência.

Preparação da dashboard (18/10): corrigida a ausência de `digest_batch.cpp`
nos targets que compilam `sha256.cpp` diretamente, causa do erro de linkagem
`TryBatchDigest` no launcher. 17559 recompilada pelo driver oficial, recibo novo
publicado e 13 artefatos conferidos. CMake de 6717/Forza corrigido pela mesma
dependência, sem recompilação desses títulos. Sem execução guest ou teste visual.
[Diagnóstico e validação](docs/reports/dashboard-build-2026-10-18.md).

Launcher (18/10): catálogo, preflight de instalação e probes saíram da thread
Win32; conclusão de sessão deixou de reinspecionar o acervo. Eventos distinguem
fechamento da janela, cleanup e saída do supervisor. SHA é reutilizado somente
em lotes com arquivos protegidos contra alteração, sem cache persistente.
Corpus sintético: conferência média de 21,60 → 11,56 s; retorno da carga na UI
32,24 s → 16 ms; saída → ocioso 1,14–1,17 s → 16–20 ms. Novas regressões: 9 casos /
107 asserções; contratos de supervisor: 25 / 1.090; catálogo: 82 / 53.799,
aprovados. Sem jogos executados ou medição real de startup/gameplay. Relatório,
limites e roteiro manual: [launcher](docs/reports/launcher-performance-2026-10-18.md).

Complemento após relato de um minuto nessa conferência: reproduzidos 84,14 / 84,88 s
no acervo local (9 alvos, 11.258 inputs, 29,50 GB lidos por passagem). A abertura
agora lista metadados em 5,98 s nas duas passagens, sem hashes do conteúdo e sem
marcá-lo como autenticado. Conferência completa permanece obrigatória ao preparar,
iniciar ou pedir verificação explícita. Resolução de diretórios é reaproveitada
somente enquanto handles impedem alteração/renomeação; links usam as verificações
anteriores. Regressões ampliadas para 14 casos / 150 asserções; catálogo 82 / 53.799.
O tempo não inclui startup real de dashboard/jogo. Evidência e limites no complemento
do mesmo relatório; nenhum jogo executado.

Charlie/NetCF, análise técnica sobre `6de07e6`: demonstrada incompatibilidade
entre o PTE aproximado do SDK e o mapper de modo de usuário do RuntimeHost.
A página `0x82BC0000` recebe `0x2BC` e falha no teste de atributos; sonda com
memória sintética reproduziu seis recusas em 15 páginas, sem execução guest.
O HRESULT da sessão histórica não foi capturado; uma falha anterior no mapper
não pode ser excluída. Sem patch especulativo ou correção funcional declarada:
contratos de PTE, entrada/saída de modo de usuário e `sc` permanecem o bloqueio.
Recibos de Charlie/17559/Moto/Castle e 2.019 arquivos originais revalidados.
Sem jogos iniciados. [Diagnóstico, testes e próximo limite](docs/reports/charlie-netcf-2026-10-18.md).

Charlie Murder, teste manual de 18/10: recibos atuais de Charlie/17559
revalidados sem rebuild de títulos. Corrigida genericamente a divergência entre
o tipo do catálogo por pasta e o perfil AOT, com arquivamento dos registros
obsoletos e do cache derivado da lista. O pedido My Games correto executou
HostLoader/RuntimeHost/NetCF, mas o bootstrap voltou sozinho antes de comprovar
inicialização XNA, imagem ou entrada do jogo. Guide/Xbox Home e gameplay não
aprovados. Uma navegação posterior em Storage → perfis caiu na dashboard 17559;
pilha preservada e causa ainda pendente. Perfis/saves originais usados somente
para gerar cópia isolada. [Evidências, correção e limites](docs/reports/charlie-functional-2026-10-18.md).

Fechamento técnico sobre a `main` inicial `6be0c60`: `ead9ea3` corrige a
invalidação de CMake removido; `d8088e6` recusa flags externas não rastreadas;
`3c42190` verifica os registros reais das DLLs; `fa5730e` separa o callback
original `0x90116C88` do sign-in 17559. Charlie foi restaurado pelo driver oficial:
31 DLLs conferidas, 27.612 registros e registro/roteamento My Games aprovados.
Motocross preservou 51.148 entradas, incluindo `0x82B36410`; o contrato de
dependência 17559 passou, e uma cópia de perfil existente passou por duas ABIs
de logon e restart sem alterar os perfis/saves originais. Sem executar jogos,
Computer Use, push ou Release. Aceite pela interface, gameplay/save/load e
Windows limpo continuam pendentes. [Relatório, commits e medições](docs/reports/audit-closure-2026-10-18.md).

Reorganização estrutural de **17/10/2026**: código/testes, ferramentas, CMake,
documentação e dados locais foram organizados por responsabilidade. Inputs agora
separam dashboards por versão e jogos por conteúdo/disco/DLC, com caminhos nos
perfis. Build Debug, desktop-only, cinco preparações e 129 regressões focadas
passaram; não houve sessão de jogo ou nova certificação funcional/visual.
[Filetree](docs/filetree.txt), [layout dos inputs](docs/guides/content-layout.md)
e [auditoria, limpeza e resultados](docs/reorganization.md).

AOT-Discovery (17/10/2026): patch 0088 separa thunks virtuais adjacentes sem
`.pdata`/RTTI e reutiliza a decodificação existente. Comparação genérica:
Iruka +2 entradas/32 bytes; Motocross +9 entradas/128 bytes, com cinco chamadas
estáticas não resolvidas reduzidas a zero. Os corpos C++ previamente emitidos
permaneceram idênticos; repetições de Iruka/Motocross foram determinísticas.
120 asserções focadas aprovadas. O entry point `0x82163C60` pertence ao
`HostLoader.dll` do Charlie e já é descoberto quando esse módulo é analisado.
Não houve execução de jogo nessa etapa nem novo aceite funcional ou de performance.
[Auditoria, corpus e limites](docs/reports/aot-discovery-2026-10-17.md).

Dependências AOT (17/10/2026): preparação genérica agora descobre XEX DLL pelo
conteúdo do inventário verificado, gera targets/registro e certifica seus inputs
por hash. Patch 0089 permite despacho opt-in no host para imagens adjacentes,
preservando bases originais e o caminho padrão dos perfis existentes.
HostLoader compilado: 1.461 funções registradas, incluindo `0x82163C60`, com
identidade/layout conferidos. Naquela tentativa, o Charlie completo ficou sem
recibo novo: o RuntimeHost falhou em labels de um switch especulativo. A falha
de compilação foi superada pelas correções PPC abaixo e pelo fechamento atual;
execução real pelo My Games e gameplay XNA continuam sem aceite.
[Implementação, reprodução e limites](docs/reports/aot-dependencies-2026-10-17.md).
Builds/recibos novos de Iruka, Motocross, Castle e 17559 aprovados. Smokes finais:
Iruka 15 s; My Games → Castle/Moto → Guide → Home original na mesma janela.
São regressões curtas de inicialização/retorno, com limitações de serviços SDK
registradas no relatório; não recertificam campanha, saves ou performance.
Castle `0x820E15E8` foi reproduzido e investigado separadamente; seu hint original
de produção permanece. [Análise](docs/reports/castle-tail-boundary-2026-10-17.md).

Correção PPC posterior (17–18/10): patch 0090 deriva o domínio de destinos de
despachos por byte sem adivinhar o tamanho da tabela, preserva casos fora do
`.pdata` e protege entradas chamadas descobertas depois. O RuntimeHost e os
32 módulos do Charlie agora compilam; recibo novo e registros nativos de
HostLoader/RuntimeHost aprovados. Patch 0091 disponibiliza o contrato genérico
de câmera ausente, com falha explícita, sem sucesso fictício. O codegen passou
oito casos/271 asserções e 3.072 chamadas nativas sintéticas. Isso ainda não
certifica execução XNA nem gameplay do Charlie. Iruka foi removido dos inputs
pelo mantenedor e não foi restaurado. A validação visual seguinte fica para o
teste manual solicitado pelo mantenedor, sem regressão automática de todo o acervo.
[Diagnóstico, falhas preservadas e resultados](docs/reports/aot-ctr-dispatch-2026-10-17.md).

Desktop (18/10): corrigida a escrita prematura do marcador de título incompleto
quando o preflight de ferramentas falha por ambiente ocupado. O driver que
possui as travas passa a ser responsável pelo marcador. Frontend Debug separado
compilado; build/recibos de Charlie, Moto, Castle e 17559 aprovados. O registro
final do Charlie na My Games ficou pendente por trava ocupada naquela etapa;
foi concluído pelo código oficial no fechamento técnico acima, sem executar o jogo.
Não há aceite funcional dos dez diretórios.

Foco da sessão (18/10): reproduzida a divergência entre foco nativo da filha e
foco SDL após iniciar em segundo plano. O supervisor agora encaminha a ativação
da raiz e reconcilia o foco após as mensagens nativas, respeitando foreground.
Seis ciclos com duas filhas sucessivas e joystick virtual passaram (89 asserções);
frontend Debug separado recompilado. Sem alteração no SDK/renderer ou repetição
do acervo. [Reprodução, correção e limites](docs/reports/session-focus-2026-10-18.md).

Motocross (18/10): patch 0092 usa o limite demonstrado pelo PPC para restringir
o domínio de switches por byte e preservar callbacks adjacentes. A entrada
`0x82B36410` voltou ao registro, sem remover entradas anteriores. Nove casos/354
asserções, 3.072 chamadas nativas anteriores e 18 chamadas pelo registro gerado
passaram; builds/recibos de Motocross e da dependência 17559 foram publicados.
O smoke sem perfil superou a falha original e chegou a avatar READY, mas caiu
em signin 17559 após 8,6 s (`0x90116C88`). A confirmação com cópia do perfil foi
recusada antes de abrir por alteração paralela de um input do recibo. O usuário
confirmou a questão do perfil e pediu finalizar aquela etapa. O fechamento atual
recompilou os produtos e verificou os contratos em cópia do perfil; nova sessão
pela My Games e gameplay continuam pendentes. [Evidência histórica](docs/reports/motocross-callable-regression-2026-10-18.md).

## Plataforma e dependências

Windows x64/D3D12 é o ambiente demonstrado. Linux, macOS, ARM64 e Windows limpo
continuam sem aceite funcional completo. ReXGlue v0.10.0 permanece fixado em
`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, LLVM 22.1.8, **104 patches ativos**
no [lock](deps.lock.json); numeração até 0105, com 0073 retirado.
O submódulo modificado pelos patches/materialização do libmspack é esperado.
Nunca substituir módulos ou fontes originais entre versões de dashboard.

## Funciona, dentro dos limites demonstrados

| Área | Evidência | Limites |
|---|---|---|
| Retail 17559 | Intro original com áudio, Home, fontes, navegação, My Games e retorno sem repetir intro | Não executa o firmware/XAM inteiro; configurações e serviços ainda parciais. [Dashboard](docs/dashboard.md) |
| Perfis locais C7 | Criação de Player1/Player2, seleção, troca, GamerCard, Sign-out e persistência pela UI original | Um usuário ativo no slot 0; sem Xbox Live, usuários simultâneos ou Guide completo. [C7.7/C7.8](docs/dashboard.md#031-múltiplos-perfis-locais-pela-interface-original-c77-2026-10-13) |
| Saves C8 | Isolamento por XUID/Title ID, flush e recuperação em processos independentes pelo caminho XAM → VFS → host | Testes de contratos; **save/load de progresso comercial ainda não comprovado**. [Armazenamento](docs/save-storage.md) |
| Castle Crashers 0.0.1.3 | Ciclo original 17559 → My Games → jogo → 17559, mesma janela; entrada e cenas observadas no escopo registrado | Trial local; campanha, licença completa e save/load não certificados. [Histórico](docs/reports/titles/validation-castle-crashers.md), [Native N3](docs/reports/graphics/native-renderer-n3.md) |
| Forza Horizon 0.0.0.10 | Ciclo My Games → condução → Guide/Xbox Home → 17559 com recibos em 14/10; perfil/entrada/áudio e retorno registrados | Campanha e saves não certificados. X2 repetiu o ciclo em harness isolado, sem recertificar o gate de preparação de produção. [Forza](docs/reports/titles/validation-forza-horizon.md), [X2](docs/reports/graphics/xenos-fetch-predication.md) |
| Motocross Madness | Tutorial histórico; correções de terreno/piloto em 16/10. Callback AOT, dependência 17559 e perfil existente em cópia conferidos em 18/10 | Novo sign-in pela interface, corridas, fidelidade e save/load pendentes. [Avatares](docs/avatar-system.md), [fechamento](docs/reports/audit-closure-2026-10-18.md) |
| Iruka (histórico) | Referência técnica com renderização/animação e regressão L1–L5 | Removido do acervo pelo mantenedor em 18/10; não restaurado. [Iruka](docs/reports/titles/validation-iruka.md) |
| Retail 6717 | Build e observações históricas de blades/navegação | Fonte correspondente ausente e smoke posterior com crash; somente diagnóstico, sem sessão guiada. [Auditoria](docs/reports/maintenance/PROJECT_AUDIT.md) |
| Biblioteca e preparação | Importação ISO/GDFX, STFS e pasta; catálogo persistente; Charlie com preparação oficial, 31 DLLs e registro/roteamento My Games conferidos | Importado/compilado não significa jogável. Execução XNA e gameplay do Charlie sem novo aceite. [Biblioteca](docs/guides/universal-library.md), [fechamento](docs/reports/audit-closure-2026-10-18.md) |
| Desktop | Interface Windows redesenhada, biblioteca, diagnóstico, seleção global de renderer e preferências | Windows limpo, acessibilidade e aceite completo de primeira utilização pendentes. [Interface](docs/guides/desktop-ui.md), [Preparação](docs/guides/desktop-preparation.md) |

## Renderização e performance

Motocross (16/10): patch 0082 permite consultar o tamanho das configurações de
perfil antes do sign-in, preservando a autorização da leitura; patch 0083
serializa as transações entre o heap físico e seus aliases, inclusive a
invalidação. Testes focados reproduziram as falhas e passaram após as correções.
Patch 0084 implementa a consulta XCTD para os streams expostos pelo VFS,
identificando a assinatura sem consumir dados nem gerar conclusão de leitura;
81 verificações focadas passaram, incluindo erros de acesso e buffers curtos.
Patch 0085 substitui o stub da máscara de usuários ociosos do microfone por
estado por runtime e atualização pendente coalescida (15 verificações); captura
de microfone continua ausente. Patch 0086 impede que carregar um XEX apague
reservas de outros módulos no mesmo heap; a tentativa com outra chave libera
somente a própria imagem e colisões são recusadas. Falhas nas duas chaves também
liberam a reserva imediatamente. Red: 12 falhas iniciais e uma de rollback;
green: 48 verificações novas e 103 do carregador/imports/codegen, sem sessão visual.
Codegen de produção aprovado sem erros de liberação; sessão Xenos posterior
com zero falhas de heap/stub de voz no snapshot de 67,548 s. Avisos de textura,
volumes ausentes e imports variáveis ainda aparecem; não é um log limpo.
O teste Native solicitado pelo usuário revelou saída 78 para textura somente
com mipmaps; esse carregamento foi corrigido e verificado por comparação CPU/GPU.
O usuário pediu retorno ao Xenos e restringiu o trabalho aos erros dos logs.
Fetch constants inválidas e fidelidade visual continuam pendentes; não há aceite gráfico nem ganho de performance
demonstrado. [Evidência](docs/reports/titles/validation-motocross-madness.md#correções-e-diagnóstico-de-16102026)
Chão preto corrigido (16/10, tarde): compressão DXT1 do terreno na GPU dependia de
fetch tipo 0 (liberado no manifesto do título) e de RT s5.10/resolve inteiro exatos
(patch 0087, D3D12 RTV). Piloto corrigido: o serviço de avatar não rodava os
construtores estáticos do XAM 17559 que calculam as escalas Y/Z do esqueleto; agora
executa o bloco verificado da tabela original. [Evidência](docs/reports/titles/validation-motocross-madness.md#chão-preto-causa-e-correção-16102026-tarde)

Idioma/localidade (16/10): seleção independente no desktop, persistida por
ambiente e repassada aos processos da sessão. Patch 0081 corrige os getters
XAM/XConfig e preserva as ABIs de idioma antigas. Dashboard/Guide Retail 17559
em PT-BR após reinicialização; Forza em português confirmado pelo mantenedor.
3 casos/800 asserções de preferências/processos e 3 casos/1.121 asserções XAM.
Escritas pelas configurações originais e outras versões ainda pendentes;
builds visuais diagnósticos, sem renovar recibos de preparação de produção.
[Evidências e limites](docs/guides/console-language-locale.md).

**Xenos é o padrão; Native está suspenso por decisão do mantenedor.**
O Guide original 17559 é composto pelo buffer de comandos de sistema da própria
XAM (patch 0078), executado no `VdSwap` de qualquer título, sem hooks por jogo e
sem compartilhar o dispositivo D3D do título. Em 16/10 abriu, navegou e fechou
na dashboard, no Forza (com Xbox Home → handoff Dashboard) e no Castle Crashers,
com o título escurecido atrás e seguindo após o fechamento; Guide → Perfil → B
volta ao Guide. A entrada fica com o Guide, inclusive botões ainda pressionados ao
fechar (patch 0079). O Perfil do Guide mostra a contagem real de jogos por
conta local, persistida e trocada com o perfil (patch 0080). Na validação de
16/10, gamerpic, Gamerscore e conquistas ainda não tinham dados locais; a
implementação de conquistas de 21/10, descrita no início deste arquivo, aguarda
aceite em gameplay. Vulkan e outros títulos não
verificados: [relatório de 16/10](docs/reports/graphics/guide-composition-2026-10-16.md)
(histórico em [15/10](docs/reports/graphics/guide-composition-2026-10-15.md)).
Games Played (16/10): sessões a partir de `2.0.6683.0` não acrescentam XBLA Trial;
anteriores preservam a inclusão. Licença do pacote e Full ativo são consultados,
sem remover registros existentes. Onze testes focados aprovados, incluindo runtime
com XEX/licença real de Castle Crashers para as versões 6717 e 17559. Builds 17559
e Castle aprovados; build 6717 bloqueado pela fonte correspondente ausente, sem
fallback nem validação visual. [Evidências](docs/games-played-trials.md).

Patches 0076/0077 corrigem resolves vazios e fetches comprovadamente inativos,
com 204 e 2.328 checks sintéticos respectivamente. Forza chegou à condução sem
os erros diagnosticados; isso não estabelece ganho global de FPS.
[Evidências Xenos](docs/reports/graphics/xenos-fetch-predication.md).

Native D3D12 permanece experimental. N3 demonstrou o ciclo Castle; N4.1 corrigiu
cores/bindless e N4.2 reduziu custo de PSOs e esperas de views inativas. Forza
chegou à condução, mas desempenho não aprovado e `EVENT_WRITE_ZPD` (0x5B)
continua não implementado. Vulkan indisponível, com recusa explícita.
[N4.1](docs/reports/graphics/native-renderer-n4-1.md), [N4.2](docs/reports/graphics/native-renderer-n4-2.md).

Instrumentação opcional/desligada normalmente. P2 não demonstrou ganho ou
sobrecusto causal nos jogos; P3 reverteu 0073 por piora de 1% low. Preservar os
resultados negativos e comparar cenas/condições equivalentes.
[Metodologia e roadmap](docs/performance.md), [P1](docs/reports/performance/performance-results-2026-10-14.md),
[P2](docs/reports/performance/performance-p2-2026-10-14.md), [P3](docs/reports/performance/performance-p3-2026-10-14.md).

## Validação e distribuição

Integração de Native/desktop/Xenos sem reescrita: build Debug completo e **22
testes focados aprovados**, com falhas iniciais e repetições necessárias descritas
no [relatório de integração](docs/reports/maintenance/integration-2026-10-15.md). Não houve nova
sessão de jogo nem ampliação de aceite de performance nessa integração.

Última suíte completa registrada, em 13/10/2026: **1.807 testes, 1.802 aprovados,
4 skips e 1 falha conhecida** em `unit.Depfile escapes characters make treats as syntax`;
136 testes project passaram. A suíte não está inteiramente verde. Esses totais
são históricos, não a contagem de uma execução nova.
[Auditoria E11](docs/reports/maintenance/PROJECT_AUDIT.md#e11-c9c12--ambiente-dos-testes-e-sessões-contínuas--13102026).

Distribuição de 18/10: a prévia **v0.1.0-preview.22** consolida as melhorias
de catálogo, builds, preparação, preflight e diagnósticos de runtime posteriores
à [preview.21](https://github.com/fabegalo/x360-native/releases/tag/v0.1.0-preview.21).
A publicação automatizada depende de build hospedado Windows x64, testes focados,
verificações de licenças, hashes e integridade do ZIP. A Release do desktop
não estabelece aceite de gameplay, XNA, save/load ou primeira utilização em
Windows limpo. [Release preview.22](https://github.com/fabegalo/x360-native/releases/tag/v0.1.0-preview.22),
[notas](docs/releases/0.1.0-preview.22.md),
[automação](docs/guides/release-automation.md).

## Prioridades e preservação

Fortalecer o Xenos conforme gargalos demonstrados, concluir os fluxos offline e
save/load comercial, e validar primeira utilização em Windows limpo. São
pendências, não autorização para iniciar novos trabalhos. Cobertura completa no
[checklist](docs/CHECKLIST_FUNCIONAL_x360-native.md).

Originais, catálogos, perfis/saves, logs/evidências, SDK pinado, patches e backups
de finalidade incerta devem ser preservados. Build/codegen são regeneráveis,
mas sua remoção exige nova preparação antes de executar. Regras operacionais em
[CLAUDE.md](CLAUDE.md); comandos em [BUILDING.md](docs/guides/BUILDING.md).
