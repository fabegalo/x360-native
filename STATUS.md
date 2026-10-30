# STATUS

30/10 — **DASH-OS-FEATURES-03, Storage parcial**: cancelamento real de I/O,
FE/`2000C`, recuperação de staging antes da quota, cálculo original de buffers
17559 e metadados persistidos de saves/perfis locais (0220–0224). Cópia HDD → MU,
move HDD → USB e exclusão com confirmação original executados, com hashes e
persistência conferidos. 0225 registra/emite callbacks reais; 0226 permite a
alocação virtual `08000000` usada pela fila original. **Aceite gráfico:** cópia
aparece na MU na mesma sessão e exclusão confirmada remove a linha obsoleta.
Cópia adicional de 256 MiB íntegra (256 hashes por origem/destino); eventos Add e
Remove chegam ao consumidor original. Preparo oficial concluído; 91 verificações
novas e 1.439 asserções/21 casos focados aprovados, replay de 10 patches/1 arquivo.
0227–0228 retiram consultas redundantes de quota/atributos: reduções locais de
65,54% e 47,14%, sem extrapolar do executor protegido; 1.337 asserções/21 casos
focados aprovados, incluindo junction real e C8. Preparo oficial concluído.
Sessão nativa com quota: save de 256 MiB excluído com confirmação, linha removida
e espaço da MU de 254 para 510 MiB; origem íntegra. A inspeção posterior do
assistente atingiu o limite de 300 s sem iniciar outra transferência.
0229 recupera pacote/header entre renames após interrupção real: 185 asserções
novas, 1.522/24 casos focados aprovados, replay de 17 patches/2 arquivos e preparo
oficial concluído. XUI exibiu “Transfer save” recuperado na MU; sessão fechada
normalmente em 76 s, hashes completos e origem preservados, sem staging.
0230 reduz a mediana do cenário de quota de 4.612 para 454 ms (−90,16%), sem
retirar proteção contra links; 1.550 asserções/25 casos focados e preparo oficial
aprovados. **Cancelamento gráfico aceito:** assistente original → Parar
Transferência → Sim, `XMsgCancelIORequest(wait=1)`, resultado real 1223; origem
de 480 MiB/8.192 arquivos íntegra, USB sem destino parcial/staging. Sessão fechada
normalmente em 210 s. Exclusão parcial da origem de move interrompido ainda não
é atômica.
Conquistas/P1 não ampliados antes de concluir Storage; sem Release.
[Evidências e limites](docs/reports/dash-os-features-03-2026-10-30.md).

30/10 — **pacote Linux x64 do launcher e gate por plataforma**: `package-desktop.ps1
-Platform linux-x64` gera `x360-native-1.12.1-linux-x64.tar.gz` (13,7 MB, candidato
local, não publicado) a partir do commit, com LLVM 22.1.8 e sysroot privado no WSL,
libstdc++/libgcc estáticos e dependências ELF só da glibc. Testes de distribuição
PASS; gate de licenças/conteúdo PASS com escopo por plataforma no inventário (Windows
inalterado: 1+24 casos; Linux: 7 negativos). Extraído e usado sem sysroot no WSL:
hashes e modos conferem, launcher abre com Vulkan, extrator empacotado leu o Castle
Crashers real (708 arquivos, 0 erros, original intacto). Piso glibc: 2.43 no
primeiro candidato; com o sysroot Ubuntu 22.04 fixado (68 `.deb` oficiais, GCC 12.3)
o `1.13.0` mede **2.35**, o gate recusa piso acima disso, e extrator, `x360n` e
launcher (software) rodaram sobre a glibc 2.35 do sysroot. Nenhuma ISO Xbox 360 local.
Linux limpo, dashboard no Linux e macOS pendentes.
[Relatório](docs/reports/linux-package-2026-10-30.md).

30/10 — **Guide 17559: fechamento pelo controlador original do HUD**: retirada
a troca instantânea do fade atual e a ocultação antes do fim da transição.
O host consulta `81695880` e continua compondo entre `VdSwap` enquanto a XAM
retorna `8000000A`, preservando entrada até terminar. A abertura inicializa o
fundo no modo 1 do export original; sucessor/pai suspenso continua sem ocultação
entre módulos. Testes focados: 677 asserções / 20 casos
aprovados, sem GPU ou sessão visual. Preparo oficial Release concluído com
recibo novo, 14 hashes de artefatos e dois de fontes conferidos. Na sessão
manual solicitada, os logs registraram vários términos da transição e troca
Guide → Perfil → Guide; primeiro processo saiu com código 0 e handoff para a
dashboard. Comparação visual quadro a quadro pendente; SDK, originais e demais
versões preservados.
[Implementação e limites](docs/reports/guide-close-transition-2026-10-30.md).

29/10 — **LAUNCHER-UNIFIED-01: launcher único SDL3 + Dear ImGui**: estado,
regras e operações extraídos para `src/desktop/app/` e um só frontend em
`src/desktop/ui/` (6 páginas, mesma paleta e disposição, ícones vetoriais
próprios, fontes Apache-2.0); frontend Win32/GDI removido. Preparação chama
`x360n prepare` com as 7 etapas reais do supervisor, sem percentual inventado;
APIs por sistema sem troca silenciosa; atualização por sistema/arquitetura.
Windows x64: compilado; controller 16 casos/179 asserções; layout headless em
5 tamanhos × 4 escalas × PT/EN, 208.685 asserções; modelo 116/54.465; smoke real
com SDL_GPU D3D12 e com rasterizador de software. CPU ociosa 0 %, memória
26,7 → 94,2 MB (D3D12). Linux x64/WSLg: mesmo target compilado, controller
15/165 e layout 203.968 asserções, smoke real com SDL_GPU Vulkan e software.
Fluxo real 17559 pelo launcher, dados isolados: recibo antigo recusado; corrigidos
o launcher (oferecia “Adicionar conteúdo” em vez de Preparar) e o `x360n
prepare` (SDK compartilhado sem os aliases `xxHash::xxhash`/Vulkan/glslang usados
pelo Native); preparação concluída, dashboard aberta, fechamento → “Sessão
encerrada” em 41 ms e foco de volta ao launcher. Texto mais nítido (sem
sobreamostragem, glifos no pixel). Extrator `x360n-extract` portado (Linux:
conteúdo 20 casos e importação STFS real pelo launcher em 18 s, original
intacto); modelo Linux 116/116 após corrigir fixtures escritos para o Windows;
Windows 117/117. Gate de distribuição aprovado (candidato local 1.10.1, sem publicação).
Sem macOS/ARM64 nem sessão da dashboard no Linux.
[Relatório](docs/reports/launcher-unified-01-2026-10-29.md).

29/10 — **sessão gráfica Linux x64 demonstrada no WSLg**: Iruka AOT ELF,
Xenos/Vulkan, janela SDL/X11 e animação/textos observados por 40 s, com dados
isolados. Preflight aprovado; supervisor encerrou no limite, zero erros/stubs
nos logs e nenhum processo de teste restante. Vulkan usou **llvmpipe na CPU**;
GPU física, áudio/controles, frontend Linux, dashboard e retorno continuam sem
aceite. Windows e dados originais preservados.
[Evidências e limites](docs/reports/linux-wslg-iruka-2026-10-29.md).

29/10 — **supervisor e pipeline AOT nativos, Windows/Linux x64**: driver C++ único
`x360n prepare`, SDK genérico por host/ABI, fingerprints de conteúdo, guard Ninja
nativo e recuperação após interrupção; wrappers mínimos. Windows preserva seus
caminhos e D3D12. WSL: supervisor com LLVM 22.1.8, processos/caminhos 182/30,
sessões 956/6 com 80 transições, integração AOT com compilação ELF real aprovada.
SDK pinado e Xenos/Vulkan Linux compilados; PPC próprio traduzido pelo ReXGlue
real e cinco testes aprovados, incluindo FP/aliasing. Estágio do preflight ELF
compilado, sem inicializar GPU. Iruka real preparado em ELF pelo mesmo pipeline;
repetição verificada sem compilar. Integração AOT Windows PE também aprovada;
contratos de processos/caminhos 112/20. Não certifica GUI POSIX, jogo/dashboard,
GPU real, macOS, ARM64, pacotes ou CI. SDK/203 patches, originais e saves intactos.
[Implementação, evidências e limites](docs/reports/cross-platform-final-2026-10-29.md).

29/10 — **CROSS-PLATFORM-FINAL parcial: processos e caminhos nativos**:
biblioteca única com backend POSIX, cancelamento/timeout/grupos, logs protegidos
e notificações por sessão; caminhos Unicode e executáveis sem sufixo Windows.
Windows: processos/IPC 48/12, crash 14/1, regressões de caminhos 45/5; launcher
Release compilado. Ubuntu/WSL x64: processos/IPC 110/20, caminhos novos 17/3 e
regressões 45/5. Três objetos Windows ARM64 compilados por cross-compilation.
Linux executou somente contratos com GCC local; aplicativo/AOT, frontend único,
macOS, outras arquiteturas, pacotes/CI e fluxos originais continuam pendentes.
Sem nova sessão visual, alteração do SDK/dados, push ou Release.
[Código, evidências e limites](docs/reports/cross-platform-final-2026-10-29.md).

29/10 — **PERF-BOOT-01 implementado, aceite visual pendente**: SDK genérico com
gerações verificadas reutilizáveis, probes/downloads paralelos e inventários
reduzidos. Setup com downloads reais + importação/projeto + preparo 17559/jogo
e registro, medidos separadamente: 1.909,702 → 1.097,788 s (**42,515%**); drivers
47,73%, objetos 2.045 → 1.210. Primeira 17559 com ferramentas prontas 10min16s;
meta <10min não atingida, setup completo 2% mais lento. Repetições 8,888/7,888 s,
sem CMake/codegen/objetos. Testes focados e builds Release passaram; 203 patches
preservados. Smoke limitado carregou o runtime, com stubs/erros registrados, sem
certificar imagem. ZIP/fluxo original My Games/retorno **NOT RUN**: Computer Use
falhou ao inicializar (os error 3). Sem push ou Release.
[Medições e limites](docs/reports/perf-boot-01-2026-10-29.md).

28/10 — **ponte original de armazenamento 17559 corrigida**: corrigido o
caminho escapado do XAM e interrompido o despacho quando sua imagem não carrega.
Ordenação e localização continuam executadas pelo código original, agora com
testes reais do carregador; red 5/36, green 534 asserções/21 casos relacionados.
Preparo oficial concluído; Settings → System → Storage → Hard Drive → Profiles
listou `P0Player1` e abriu seus detalhes em sessão isolada de 180 s. Teclas
direcionais simples foram remapeadas somente no diagnóstico; miniatura com
ícone de aviso e mover/excluir conteúdo continuam sem aceite. Sem Release.

28/10 — **IMPORT-17559 implementado, aceite gráfico pendente**: ZIP oficial,
pasta `$SystemUpdate` e PIRS reconhecidos por conteúdo; flash 17559, recurso
`shrdres` do XAM e avatar organizados pelos SHA-256 do perfil. O ZIP local
fornece 14 arquivos, mas JKLatin/CLatin vêm somente como XTTP: duas fontes
completas exigidas, sem falsa prontidão. Build Release isolado, 493 asserções
em 21 casos focados e 12 controles de fontes LGPL aprovados. O helper gráfico
bloqueou o seletor mesmo após reserva; fluxo gráfico completo, novo preparo
AOT e boot com esses inputs não certificados. SDK/XAM/patches/renderers e dados
preservados; sem Release. [Evidências e limites](docs/reports/import-17559-2026-10-28.md).

28/10 — **SYSTEM-UI-P0: teclado original dentro dos jogos**: 0215/0216
encaminham Keyboard/Branded aos pedidos originais 17559, sem aceitar o texto
padrão automaticamente; entrada SDL UTF-16, foco modal e restauração dos controles.
Corrigido separadamente o carregamento de vk.xex pelo CreateProfile no namespace
verificado da dashboard, em vez do HDD do jogo. Windows x64/Xenos: edição,
confirmação e cancelamento na dashboard; no Minecraft, limite do buffer e
retorno ao menu. Guide → Sign In → Create Profile → nome editado `P0Player1`
criou XUID persistente, fez logon e retornou ao tutorial; aceite do mantenedor.
Reinício recuperou o mesmo XUID no jogo e no Guide; Sign-out registrado com
resultado 0 e confirmado pelo mantenedor. 992/22 verificações focadas, mais
375/19 após conferir a limpeza de falhas do host; replay 203 patches/273 hashes.
Complemento manual: My Games → Minecraft → Sign-in existente → Guide → Xbox
Home retornou à dashboard original com `P0Player1`, mesmo XUID e estado 1.
Três processos concluíram normalmente (47/31/47 s), em janelas separadas;
captura da janela embutida não validada. Dashboard e jogo na main concluíram
preparo oficial com 203 patches. Demais serviços offline continuam pendentes;
sem nova homologação de saves, recertificação geral ou Release.

28/10 — **MAINT-01: limpeza executada**: 17.794 arquivos e 1.325 diretórios
vazios de ensaios aposentados removidos; 38,442 GiB recuperados no volume.
Originais, perfis/saves reais, SDK, preparações e STABLE-01/02/03 preservados.
Planos, hashes, journal, guardas de processos/links e prevenção no empacotamento;
20 testes de limpeza, 23 do gate e três CTests focados passaram, sem jogos ou UI.
Main avançada em paralelo foi preservada. [Relatório e limites](docs/reports/maintenance/maint-01-2026-10-28.md).

28/10 — **STABLE-03: quatro aceites manuais confirmados pelo mantenedor** no
ambiente `Desktop/x360-release`, Preview 25/`9abb880`. ZIP e 1.178 arquivos
conferidos; recibos Retail 17559 e Forza Horizon 0.0.0.10 íntegros (4.468/4.534
referências). Xenos/D3D12 e RTX 4070 documentados; Windows limpo/update e
save/load aceitos por relato, com lacunas de metadados históricos preservadas.
MAINT-01 revisada e integrada; 14 testes de manutenção e tooling de release
aprovados. Preparação local `v1.0.0-rc.1`, sem publicação definitiva. Gate e ZIP
finais têm recibos próprios, sem herdar PASS manual de outra identidade.
[Homologação e matriz](docs/stability/homologation-stable-03-2026-10-28.md).
Native/Vulkan continuam experimentais; SDK/201 patches/originais preservados.

27/10 — **STABLE-02: main `f84436d` recertificada nos 37 contratos**:
nova execução, sem reaproveitar o FAIL histórico de `9abb880`; relançamento com
preferências e 80 transições aprovados. A correção `c788f9d` já está integrada.
Build Release e integridade dos 201 patches/271 arquivos SDK aprovados;
catálogo, update recovery e crash/relançamento sintéticos PASS, 23 controles
negativos PASS. Runner e verificador recusam v1.0.0 somente pelos quatro NOT RUN.
Os quatro aceites reais continuam NOT RUN e impedem certificação v1.0.0.
Console 17559 completo e cache Forza 1 copiados para seed/working isolados,
3.095 arquivos por cópia, hashes e origem preservados. Forza Horizon 1 integral
é o candidato recomendado; requer renovar preparo, licença/conteúdo autorizados
e autorização específica antes do ensaio real. [Execução e matriz](docs/stability/execution-stable-02-2026-10-27.md),
[roteiro e bloqueios UI/Windows limpo](docs/stability/persistence-rehearsal.md).
SDK/renderer preservados; sem gameplay, Computer Use, tag ou Release.

27/10 — **STABLE-01 iniciada sobre Preview 25 / main `9abb880`**: contrato
candidato Windows x64/Release/Xenos D3D12/Retail 17559 e gate de evidências
obrigatórias em [docs/stability](docs/stability/README.md). A certificação segue
bloqueada: save/load comercial e Windows limpo sem aceite; uma falha no
relançamento com preferências na main foi preservada. Colisão de diretórios
de diagnóstico no mesmo segundo foi identificada e corrigida na candidata.
Novos controles negativos, proteção do HDD em crash/relançamento e atualização
sintética não certificam conteúdo original. [Execução e matriz](docs/stability/execution-2026-10-27.md).
SDK/renderers preservados; sem Computer Use, jogos, tag ou Release nesta etapa.

27/10 — **auditoria da preview.25**: main inicial `b50671c6…18e3`, replay
limpo de 201 patches/271 hashes sem divergências, Release D3D12/Vulkan,
903/28 XAM/UI e 767/19 AOT aprovados. Xenos: 2.324 casos/API e 45 diferenças
R8/API históricas preservadas. Quatro probes Native falham de modo idêntico ao
baseline; triagem N-PERF-01 não mostrou regressão grave reproduzível. Corrigidos
o include RenderDoc do probe Vulkan, seu teste de ABI e dois fixtures de sessão,
sem enfraquecer as rejeições de produção. 17559 concluiu preparo novo; Minecraft
chegou ao tutorial, e A/B concluíram a caixa original nos dois jogos autorizados.
Três tentativas limitadas sem crash e 37/37 arquivos de origem preservados em
cada. Movimento Minecraft/condução Forza não recertificados; outros recibos
locais antigos continuam exigindo preparo. [Auditoria e NOT RUN](docs/reports/release-audit-2026-10-27.md),
[notas da candidata 0.69.27-preview.25](docs/releases/0.69.27-preview.25.md).

27/10 — **caixa de mensagem original validada**: snapshot anterior publicado
em `origin/main` (`fb99547`). 0213 substitui o caminho ImGui/headless-default de
`XamShowMessageBoxUI` pelo pedido original FE/2100D de 1.148 bytes. O host 17559
carrega seu próprio HUD original, com cenas, fontes e skin já verificadas, sem
condição por jogo. Controles iniciais: 217/2 da API, 688/22 relacionados e 185/4
de input. Teste adicional revelou a troca do
solicitante por um worker ao completar o I/O (1/13 falhou); 0214 corrige esse
contrato; 903 verificações/28 casos focados aprovados e replay final de
201 patches/271 arquivos sem divergências. Minecraft e host 17559 concluíram
preparo oficial. Duas tentativas Minecraft isoladas de 120 s mostraram a caixa original:
B cancelou com 1627/1223; A selecionou o botão 1 com sucesso, liberou o foco
e retornou à tela do Minecraft. Sem crash; 37/37 originais preservados em ambas.
O toque para cima não mudou o destaque; navegação entre botões ainda sem aceite.
Forza também concluiu preparo oficial com 0214 e mostrou a mesma caixa:
B devolveu 1627/1223 e retornou a “Pressione Start”; 90 s sem crash e 37/37
originais preservados. Os três produtos estão atualizados, sem marcador incompleto.
Diagnóstico visual opcional, desligado normalmente, sem escolha automática.
Os resultados de gameplay 0212 abaixo são históricos; esta etapa valida a UI.
[Implementação e evidências](docs/reports/original-message-box-2026-10-27.md).

Retomada de 26/10 — **Forza Horizon 2 e Minecraft pelo preparo genérico**:
a exclusão do firmware de disco está integrada em `e407eb8`. A análise real
do Forza revelou seis destinos diretos sem função; 0203 recupera entradas
ocultas por wrappers, e 0204 dá precedência a destinos explícitos sobre gaps
especulativos, sem hints por título. Ambos concluíram Release/recibo, mas as
primeiras execuções caíram antes do menu em callbacks sem entrada AOT (Minecraft
`8233F020`, Forza `83243750`), referenciados nos dados originais. 0205 acrescenta
descoberta conservadora dessas entradas sem RTTI. SDK pinado e runtime preservados;
aplicação/hashes, replay e 200 verificações em dez casos `[gapfill]` aprovados.
Ambos concluíram preparo com 0205 e superaram esses callbacks. Minecraft agora
falhava numa leitura guest `000001A4`; o diagnóstico opcional revelou longjmp
traduzido como retorno C++ comum. 0207 reconhece automaticamente o CRT completo
e seleciona os hooks nativos existentes. Forza superou a exaustão de thunks com
0206 (identidade/reuso/invalidação de exports; 139 verificações em quatro casos),
mas encontrou callback montado por lis/addi sem entrada. 0208 descobre endereços
em bloco conhecido, sem clobber, com as proteções de metadados/corpo de 0205.
Na conferência real, 0208 confundiu tabelas de salto inline com código; esse
produto não foi executado. 0209 registra os bytes da tabela no grafo e protege
seus intervalos. CRT/fronteiras/callbacks: 360 verificações em 13 casos; CTR:
234/4; replay completo 196 patches/267 arquivos, zero divergências.
Ambos concluíram preparo oficial com 0209.
Minecraft com 0209 concluiu Release/recibo e uma tentativa de 180 s sem exceção
não tratada/chamada ausente; o supervisor encerrou no limite. Sem observação
visual/input, não comprova menu ou gameplay. Forza também concluiu Release/recibo
e 180 s sem exceção não tratada/chamada ausente; continuam quatro erros de
dispositivos/app F1 e um stub. Seed 28/28 preservado em cada tentativa.
O observador de falhas fica desligado normalmente.
Em 27/10, o usuário liberou os dois executáveis no Computer Use. Minecraft
mostrou o menu e o mundo 3D do tutorial; movimentação e interação ainda sem aceite.
Forza mostrou a abertura e o aviso “No Device Selected”. O diagnóstico direto
omitia o contexto da sessão de console, incluindo o seletor original existente.
Diagnóstico corrigido para compartilhar os argumentos da sessão: 32.125
verificações/12 casos aprovados. Preparo 17559 concluído em `78cdd481…`;
diagnóstico isolado Forza abriu o seletor original, concluiu HDD 1 e criou os
arquivos do perfil do jogo. Caiu depois na leitura de configurações em
`82E2DD2C` (guest `0008039D`; repetição `00000368`). Minecraft abriu o seletor,
cancelou com 1627/1223 sem escrever o dispositivo e completou 60 s sem crash.
28/28 originais preservados em cada tentativa. Captura opcional ampliada para
16 blocos de 64 bytes/um salto de ponteiro: 62 verificações/2 casos; lote XAM
de 29 configurações: 113/1, correto isoladamente. Trace DEBUG 0210 (311/8;
replay 197 patches/267 arquivos) mostrou retorno assíncrono sem serialização.
Probe CPU com XEX e perfil reais reproduziu ERROR_NOT_FOUND nos totais:
a cópia do diagnóstico omitia metadados de conquistas de jogos anteriores,
embora copiasse seus desbloqueios. Com a cópia completa, síncrono e assíncrono
preenchem os 29 campos com sucesso. Corrigida a cópia isolada; Forza superou
essa queda e mostrou céu 3D/carregamento, mas caiu depois na troca de fibers
do CRT, ainda traduzida sem transferência da continuação C++. 0211 seleciona
os hooks nativos existentes somente após reconhecer o ciclo CRT completo;
706 verificações/18 casos compartilhados, quatro trocas nativas em CPU,
135 verificações de descoberta/overrides e replay 198/267 aprovados. XEX real
reconhecido sem hints; Forza/17559 concluíram preparo. No jogo, as fibers antes
problemáticas executaram e encerraram corretamente e apareceu a tela de
controles. Nova queda posterior em outra thread lê uma lista nula de comandos
gráficos (`82426CF4`, guest 4), após 90,8 s; causa em investigação com captura
opcional do supervisor, sem mudança de renderer ou bypass. Minecraft com 0210 concluiu preparo e 180 s:
Computer Use observou tutorial 3D, deslocamento, extração e vara de pesca;
37/37 arquivos originais preservados, encerramento no limite, sem crash.
Nova tentativa com Start físico encontrou callback ausente `82460DC8` na fila
de comandos. O decoder de análise desconhecia rldicr, já suportado pelo emissor.
0212 implementa as quatro rotações MD na análise, sem relaxar fronteiras:
767 verificações/19 casos e 32.768 combinações, callback real recuperado sem
erros, replay 199 patches/271 arquivos sem divergências. Forza e 17559 concluíram
preparo oficial com 0212. A tentativa 15 chegou à condução “Dirija até o Festival”:
HUD/minimapa, 138–206 km/h, distância 9,3 → 6,9 km, 180 s sem crash e 37/37
originais preservados. A lista gráfica nula anterior não reapareceu neste teste;
sua relação com o callback corrigido ainda não está comprovada.
O preparo Minecraft antes interrompido foi concluído oficialmente com 0212:
287 fontes geradas têm os mesmos hashes do build 0210. Nova tentativa de 120 s
usou a configuração global Xenos D3D12, sem override de renderer, e mostrou
tutorial 3D, deslocamento e extração de madeira na hotbar, sem crash; 37/37
originais preservados. O aviso de versão de avaliação continua explícito.
Configuração local global alinhada a Xenos D3D12, fullscreen preservado e cópia
anterior guardada nas evidências; não houve mudança de renderer ou ajuste por título.
Sem seleção automática de HDD ou mudança do contrato XAM para esconder o erro.
**Ambos com gameplay inicial demonstrado no build 0212: Minecraft no tutorial
de avaliação e Forza na condução até o festival. Save/reload comercial,
campanha completa, Native e sessões longas não certificados**.
Testes visuais/Computer Use autorizados somente nesses jogos, em cópias isoladas,
com três minutos por tentativa. [Diagnóstico e estado](docs/reports/titles/generic-forza-horizon-2-minecraft-2026-10-26.md).

Complemento de 25/10 — **investigação RGB565 sobre `5b87e8c`**:
os logs 0201 identificam as 45 diferenças/API como `format=2` (**R8**),
não RGB565 (`format=4`). Os 72 controles RGB565 originais passam nas duas APIs.
A ampliação para 344 casos/API encontrou 120 falhas preexistentes de MSAA
escalado; referência com helpers Canary e oráculo racional concordam nos 344.
Candidato local correto numericamente foi rejeitado por regressão Vulkan;
renderer, SDK, lock e todos os 60 recursos full/gamma/fixed16 preservados.
As 45 falhas R8 continuam reproduzíveis, sem mudar expectativas. Sem novo
patch SDK ou aceite de gameplay. Próxima prioridade: pipeline Xenos sintético
rasterização → EDRAM → resolve → readback em D3D12/Vulkan.
[Evidências, reprodução e pendências](docs/reports/graphics/rgb565-resolve-2026-10-25.md).

Complemento de 25/10 — **0201: full resolve fixed16** sobre `c6f091e`:
confirmados bytecodes antigos com `32/32767` e clamp em −1; s5.10 exige
`1/1024` e preservação dos negativos. As 48 divergências/API foram eliminadas.
RTX 4070: 864 fixed16/API, 684 gamma/API e 504 controles/API aprovados;
45 diferenças R8/API preservadas separadamente (identificação corrigida acima).
CPU: 291.132 verificações
e 11 regressões focadas. Release Xenos/Native com D3D12/Vulkan, regeneração
determinística e replay de 188 patches/256 hashes aprovados, SDK pinado mantido.
Compute headless não certifica rasterização completa nem gameplay.
[Causa, validação e medições](docs/reports/graphics/full-resolve-fixed16-0201-2026-10-25.md).

Estado consolidado em **2026-10-23**; a referência funcional integrada histórica é `1844245`.
Este arquivo resume o estado atual; os relatórios vinculados preservam datas,
falhas iniciais, hashes, comandos e limites de cada validação. Resultados de uma
etapa histórica não certificam automaticamente o build atual nem outros títulos.

G3 — integração de código em 23/10: Native D3D12/Vulkan independentes com ZPD,
visibilidade/predicação, MEMEXPORT, tesselação, primitivas e formatos adicionais.
Enhanced integra escala 1–4× até resolve/textura/scanout, AF, FXAA, CAS/FSR1 e
perfil Forza identificado para FPS/tempo decorrido, ultrawide e HUD central.
Xenos ganhou uploads Vulkan com dependências, submissão ordenada em worker,
espera correta por pipelines e rastreamento esparso de páginas CPU, preservando
FPS e resolução originais. SDK pinado preservado, 165 patches ativos; Native,
Xenos, runtime, supervisor e launcher compilaram em Release (build5, saída 0).
Preparo oficial final de Forza e dashboard 17559 concluído em 23/10 às 01:05 BRT,
com 7/14 artefatos e 4.472/4.401 entradas autenticadas, sem marcador incompleto.
O mantenedor transferiu testes de execução e medições para depois; não há novo
aceite de gameplay nem ganho medido. Native continua experimental e a seleção
Vulkan no launcher respeita o gate de título real solicitado.
[Entrega e evidências](docs/reports/graphics-g3-2026-10-22.md).

Criação de perfil — retomada 22/10: no run `20261022T021930Z` (aplicativo
principal, sem sessões concorrentes) Entrar → Criar perfil criou dois perfis com
logon e a dashboard continuou aberta até o mantenedor fechá-la. O atalho direto
do Guide chamava `XamAppRequestLoad`/`XamAppLoad("createprofile.xex")`, ainda
stubs (registrados nos logs manuais). Patch 0131 os implementa por um hook do
perfil; o host 17559 abre o CreateProfile original sem pedido (hipótese
documentada), sem fallback. `[app-load]` 40 asserções/2 casos; relacionados
1.977/38. Preparo oficial às 23:48 (21/10, horário local): 13 artefatos/4.032 entradas
conferidos. Atalho direto aceito pelo mantenedor (run `20261022T025119Z`). Patch 0132:
`XamUserIsPartialProfile` (stub) fazia o Guide abrir as conquistas do título
atual em vez da lista de Jogos para os controles 1–3; red 5/10 → green, produto
`4bb5a0fe…` conferido. Tela de conquistas de um jogo carregava sem parar: a
`LoadAchievementsScene` pede flags `0x27` e o SDK recusava bits fora de `0x7`
(original `81670190` não valida). Patch 0134 corrige (red 87 → green; sonda com
cópia dos dados reais devolve as 50 conquistas do FM4); 0133 só registra log.
Aceite visual pendente.

Criação de perfil — teste manual às 22:22 (21/10): Entrar → Criar perfil
gravou a identidade e concluiu logon, mas caiu no retorno; atalho direto do
Guide também sem aceite. Run `C0000005`, sem pilha/PPC capturados. Patch 0129
distingue fechar toda a pilha de fechar só a tela; host 17559 deixa de restaurar
pais concluídos e libera cópias de strings do estado descartado. Release focado
0129/0130 e ajuste do host: **1.161 asserções / 23 casos**, mais sondas CPU dos
métodos originais de fechamento e serialização. O host aceita pedidos durante
a inicialização e não restaura um buffer recusado com `S_FALSE`. SDK com 130
patches; preparo `69e532…`, integrado ao seletor `5898c6…`: 13 artefatos/4.031 entradas/15 originais
conferidos. Quatro sondas CPU com a DLL exata do produto passaram, sem erros ou
stubs; 15 arquivos reais preservados. Causa exata do acesso inválido e aceite
físico das duas rotas continuam pendentes.
[Diagnóstico e limites](docs/reports/profile-guide-p0-2026-10-21.md).

P0 Storage 17559 — 22/10: HDD único da sessão (0136; sem migração, por decisão do
mantenedor) para o Storage ver saves de todos os jogos; registros agregados de 584 bytes
com tamanho (0137); perfis como pacotes de conteúdo por dispositivo, listáveis, apagáveis
e copiáveis/movíveis no Storage (0138, sem migração; aceite visual pendente). Copiar/mover (S4) implementados no patch 0135, transacionais,
com progresso/cancelamento do callback original da dashboard, cota e recusa de conflito;
`[storage-transfer]` red→green (58 asserções), relacionados 2.387/39. Roteiro manual
descartável `scripts/validation/start-storage-transfer-17559.ps1`; aceite visual pendente.

P0 Storage 17559 — incremento S1 e parte de S2: HDD real persistente, MU/USB
configuradas com IDs estáveis, quota/reserva, capacidade NT/XAM coerente e
seletor original FE/2100B com AOT próprio, sem escolher HDD automaticamente.
Patches 0120/0121/0122/0127 de conteúdo preservam C8/XUID e os originais 17559.
Release integrado: 2.690 asserções/43 casos; roteamento original: 25/um caso.
Produto final `5898c6…`: 13 artefatos, 4.031 entradas e 15 originais conferidos.
Teste curto autorizado abriu Sistema → Armazenamento e o seletor original com
HDD/MU, capacidades coerentes e categorias originais. A UI revelou cache
fictício de 512 MiB por BOOL stale: patch 0130 corrigiu, e o reteste mostrou
Itens do Sistema em 0 KB. A abertura do seletor revelou helper AOT ausente;
limites originais de ceil/floor adicionados, com sonda CPU numérica aprovada.
Seleção concluiu HDD1, resultado 0; cancelamento concluiu 1627/extended1223,
saída de dispositivo intacta. Houve entrada manual concorrente; escolha da MU,
automação exclusiva e retorno completo ao Home pelo Storage continuam sem aceite.
A sessão de cancelamento foi encerrada intencionalmente pelo agente para liberar
teste manual de perfis; esse fechamento não é nova queda do produto. Dados
descartáveis preservados. Copiar/mover, formatação,
transações/recuperação, integração completa com My Games e save/load comercial
continuam pendentes; o sistema solicitado não está completo.
[Matriz, ABIs, evidências e aceite](docs/reports/storage-p0-2026-10-21.md).

P0 perfis — correções adicionais dos logs manuais: patches 0123/0124/0125
tratam campos opcionais ausentes sem erro global 87, enviam OptionsUI ao HUD
original e implementam flags locais XGI B0082 por XUID. Patch 0128 remove os
resultados fictícios do parser de gamerpic, seguindo o export original 17559.
Outros 23 callbacks originais da família das páginas de perfil foram auditados
e incluídos na análise AOT. Release focado integrado com Storage/gráficos:
**2.690 asserções / 43 casos**, sem suíte completa ou execução de interface.
SDK Check integral passou com 128 patches; replay limpo reproduziu 130 arquivos
finais. Preparo oficial da 17559 concluído às 22:19:03, coordenado com Storage
e gráficos: 13 artefatos, 4.027 entradas e 15 originais conferidos, sem marcador
incompleto. Dez arquivos reais de identidade/conquistas e os 114 patches
históricos preservados. Sonda GamerProfile original passou duas enumerações
overlapped de jogos/conquistas por perfil na sequência Alpha → Bravo → Alpha,
com isolamento de pontos/flags; parser HLE coincidiu com o export original em
seis entradas. Ambas usaram a DLL exata do produto e saíram 0, sem erros/stubs,
em armazenamento temporário e sem entrypoints, cenas, GPU ou jogos.
Queda guest 5 após criação, seleção completa de avatar/gamerpic e aceite das
cenas originais continuam pendentes. [Detalhes](docs/reports/profile-guide-p0-2026-10-21.md).

P0 perfis/Guide (21/10): implementadas entradas originais de jogos/modo 1,
jogos com prêmios/modo 6 e edição/modo 3; renomeação mantendo o XUID; remoção
da identidade com snapshot recuperável; preferências persistentes de sete tipos
com cache por XUID/Title ID, falhas e proteção da leitura concorrente.
Patches novos 0116–0119 preservam os 114 anteriores e o SDK pinado.
Testes Release relacionados: **2.060 asserções em 31 casos**, sem suíte completa.
Sonda headless executou duas criações e logons pelos métodos originais 17559,
em armazenamento novo; isso não executa o retorno das cenas à dashboard.
O acesso guest `0x00000005` relatado após criação continua sem causa confirmada
e sem aceite da correção. Diagnóstico PPC opcional permanece desligado nas
sessões normais. Build Release integrado concluído pelo driver oficial;
12 artefatos e 14 originais conferidos. Sondas com a DLL exata do produto
passaram nos dois pares de criação/logon originais e no diagnóstico sintético.
O supervisor foi corrigido para retorno da dashboard à própria dashboard sem
encerrar a plataforma; supervisor Release usado pelo aplicativo atualizado e 57 asserções/quatro
casos relacionados passaram. Total final: **2.117 asserções / 35 casos**.
Dez arquivos reais de identidade/conquistas mantiveram seus hashes.
Sem execução de dashboard/jogo ou controle de UI pelo agente. Um usuário ativo
no slot 0; quatro usuários simultâneos e o fluxo visual completo de conquistas
continuam pendentes. [Evidências e limites](docs/reports/profile-guide-p0-2026-10-21.md).

Sessões manuais 21:02–21:08 (21/10): queda fatal ao abrir preferências de
entrada (`803_SignInPreferences.xur`), chamada AOT ausente `817ACC88`,
LR `817B3928`, saída `C0000409`. Confirmado callback de duas instruções no
original 17559 e incluído na análise. Sonda headless falhou com a DLL anterior.
Também identificada cópia antiga do supervisor ao lado do launcher; staging
passa a ocorrer em todo build, mesmo sem relink. Os erros de settings,
`XamShowOptionsUI` e XGI receberam as correções adicionais descritas acima;
`DASHUSER:`, `shrdres.xzp` e os pedidos online continuam sem correção certificada.
Build Release oficial concluído, recibo com 12 artefatos conferidos e sem
marcador incompleto; sonda do callback passou com a DLL final. As cópias do
supervisor agora têm hashes iguais; link do frontend encontrou arquivo
indisponível, sem encerrar o aplicativo aberto. Dez arquivos de usuário e os
14 originais permaneceram idênticos durante esta correção. Sem aceite visual novo.

Inspeção da sessão manual das 20:02–20:03 (21/10): saída `0` após
`XamLoaderRebootToDash reason=23`/handoff `Dashboard`; sem reprodução de
CreateProfile. As chamadas `XamShowGamesUI` e `XamShowAvatarAwardGamesUI`
atingiram os stubs que motivaram as novas rotas. O encerramento após retorno à
própria dashboard motivou a correção do supervisor. Sem captura PPC/pilha nessa
sessão; o fluxo visual do novo build ainda precisa de aceite. Detalhes no
relatório P0 acima.

Forza física (22/10): `vmsum3fp128`/`vmsum4fp128` devolviam ±inf em vez do
QNaN do Xenon quando entradas finitas transbordam float32. Patch genérico 0141
e `sdkcheck.Vmsum` (falha antes, passa depois); `ppc.*` do SDK passa. Os
endereços do GJK citados pelo ReImagined (0.0.0.7) existem no nosso 0.0.0.10;
AOT do Forza regenerado em Release (2738 sites). Colisões e a queda de 302 s
acima **não** foram retestadas: a relação com o GJK é hipótese. Relatório:
[forza-vmsum-collision-2026-10-22](docs/reports/forza-vmsum-collision-2026-10-22.md).

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
`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, LLVM 22.1.8, **201 patches ativos**
no [lock](deps.lock.json); numeração até 0214, com retirados fora da cadeia ativa.
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

**Xenos é o padrão; Native continua experimental, retomado na G3 de 22/10.**
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

G1 (21/10), **incremento parcial**: build Release raiz com Xenos/Vulkan e
encaminhamento explícito da API; scanout e D1OVL offscreen passaram, incluindo
BGRA/alpha, gamma e resize. Cinco capturas equivalentes entre APIs foram
idênticas. Native/D3D12 usa arenas aposentadas por fence; o microcenário
controlado de 4.096 draws preservou a imagem e teve medianas de processo de
1,421 → 1,332 s, sem comprovar FPS/frametime de gameplay. Configuração, ABI e
transições sintéticas: 940 asserções/sete casos relacionados aprovados.
Native/Vulkan e ZPD continuam pendentes; títulos/AOT, recibos e ZIP Vulkan
ainda não estão integrados/validados. O preparo 17559 de perfis/Storage permanece
Xenos/D3D12; não foi alterado para Vulkan nem houve sessão original nova nesta
etapa. [Implementação, baseline, falhas e matriz](docs/reports/graphics/g1-vulkan-native-2026-10-21.md).

Native D3D12 permanece experimental. N3 demonstrou o ciclo Castle; N4.1 corrigiu
cores/bindless e N4.2 reduziu custo de PSOs e esperas de views inativas. Forza
chegou à condução, mas desempenho não aprovado e `EVENT_WRITE_ZPD` (0x5B)
continua não implementado. Naquele checkpoint, Vulkan estava indisponível,
com recusa explícita; o incremento G1 acima ainda se limita aos probes.
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
