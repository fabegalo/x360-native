# STATUS

Estado consolidado em **2026-10-18**; a referência funcional integrada histórica é `1844245`.
Este arquivo resume o estado atual; os relatórios vinculados preservam datas,
falhas iniciais, hashes, comandos e limites de cada validação. Resultados de uma
etapa histórica não certificam automaticamente o build atual nem outros títulos.

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
`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, LLVM 22.1.8, **91 patches ativos**
no [lock](deps.lock.json); numeração até 0092, com 0073 retirado.
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
conta local, persistida e trocada com o perfil (patch 0080); gamerpic,
gamerscore e conquistas seguem sem dados locais. Vulkan e outros títulos não
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

Prévia pública mais recente conferida em 18/10 pela API GitHub:
**v0.1.0-preview.21**, publicada em 16/10 às 05:50:07 UTC, com ZIP e SHA-256.
A main inclui trabalho posterior que não foi publicado nesse pacote. A conferência
foi de metadados, sem recertificar/download do ZIP. Nenhuma nova Release resulta
desta auditoria. [Release existente](https://github.com/fabegalo/x360-native/releases/tag/v0.1.0-preview.21),
[notas](docs/releases/0.1.0-preview.21.md),
[primeira Release](docs/reports/maintenance/FIRST_RELEASE.md), [automação](docs/guides/release-automation.md).

## Prioridades e preservação

Fortalecer o Xenos conforme gargalos demonstrados, concluir os fluxos offline e
save/load comercial, e validar primeira utilização em Windows limpo. São
pendências, não autorização para iniciar novos trabalhos. Cobertura completa no
[checklist](docs/CHECKLIST_FUNCIONAL_x360-native.md).

Originais, catálogos, perfis/saves, logs/evidências, SDK pinado, patches e backups
de finalidade incerta devem ser preservados. Build/codegen são regeneráveis,
mas sua remoção exige nova preparação antes de executar. Regras operacionais em
[CLAUDE.md](CLAUDE.md); comandos em [BUILDING.md](docs/guides/BUILDING.md).
