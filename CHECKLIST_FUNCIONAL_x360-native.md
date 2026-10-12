# Checklist funcional — x360-native

**Objetivo:** escolher uma dashboard original → executar a intro correspondente → entrar em um perfil → listar e selecionar um jogo pela interface original → atender ao pedido original de lançamento → jogar → salvar → atender ao pedido original de retorno → voltar à mesma dashboard, sem repetir a intro.

Windows x64; recompilação estática com ReXGlue; suporte progressivo a todas as versões originais de dashboard, incluindo antigas e betas, e ao catálogo de jogos Xbox 360.

**Legenda:** `[x]` é um marco relatado pelo usuário/agentes, somente no alvo e no alcance indicados. Não houve verificação direta do repositório nesta elaboração. `[ ]` significa requisito ainda sem demonstração completa nos relatos; não prova ausência de implementação. Os itens gerais precisam ser validados por versão, jogo e funcionalidade. Um recurso só é exigido numa dashboard quando faz parte daquela versão.

**Prioridades:** P0 = fechar uma experiência offline utilizável; P1 = ampliar compatibilidade e fidelidade; P2 = funcionalidades avançadas e dependências externas. As prioridades não excluem itens da meta final.

## Estado atual consolidado

| Área | Último resultado relatado | Limite da evidência |
|---|---|---|
| Intro + dashboard 17559 | Animação e áudio originais, conclusão pela exportação 2 e transição para a dashboard; fonte original integrada. | O tempo observado de cerca de 11 segundos não é um temporizador nem um requisito fixo. |
| My Games | A dashboard chamou `XamContentLaunchImageInternalEx` para `XSYSLAUNCH:\default.xex`. | Comprova a origem do pedido; não aprova enumeração geral, resolução de qualquer título ou o ciclo completo. |
| Supervisor | Conferiu caminho e hash da recompilação do Castle Crashers, segundo o agente. | Ainda falta explicitar qual hash e a ligação entre conteúdo selecionado, XEX e artefato nativo. |
| Retorno original | Castle Crashers chamou `XamLoaderLaunchTitle(NULL, 0x100)` e voltou à 17559 sem intro; confirmado pelo usuário e logs (§0.19). | Guide, perfil selecionado e saves não validados. |
| Motivo de encerramento | Protocolo distingue Dashboard, TitleTerminate, GuestExit e WindowClose; testes de exports e handoff passaram. | Ainda falta testar cada cenário e repetição na interface real; fechar janela não conta como retorno Xbox. |
| Castle Crashers | Chega ao menu em execução separada; há regressões relatadas sem crash. | Não aprova, por si só, seleção genérica, perfil, saves ou retorno original pela dashboard. |
| Dashboard 6717 | Perfil separado criado e executado. | Último bloqueio relatado: import XAM `0xAE6`, chamado em `0x9210F1F4`; sem novo resultado visual apresentado. |
| Perfis e ciclo completo | Ainda sem demonstração completa nos relatos. | Não presumir implementação ausente nem marcar como concluído. |

**Critério de fidelidade:** o código original da dashboard/jogo deve originar os pedidos, e o ambiente deve respeitar sua identidade, seus argumentos e seus efeitos. O supervisor pode ser uma implementação interna no PC; clique interceptado, jogo fixo e fechamento de janela não substituem os pedidos originais. Usar um ou mais processos, isoladamente, não comprova nem invalida essa fidelidade.

**Evidência posterior — 2026-10-12:** o usuário confirmou o ciclo My Games →
Castle Crashers → retorno pelo jogo → 17559 sem intro. Logs e limites em
`docs/dashboard.md` §0.19: pedido `XamContentLaunchImageInternalEx`, retorno
`XamLoaderLaunchTitle(NULL, 0x100)`, 1020 bytes preservados, mesmo HWND.
31/31 sdkcheck e 40/40 x360n passaram. Isso não aprova perfis, saves, Guide ou
fidelidade visual da troca: o usuário relatou **corte seco ao lançar pela biblioteca**.
A continuidade deve seguir o caminho original, sem fade ou tela inventados.
Os itens gerais abaixo continuam pendentes quando a evidência cobre só este par.

## 1. Inicialização e seleção da dashboard — P0

- [x] Executar a intro original da 17559 com animação e som.
- [x] Encerrar a intro pelo mecanismo original, incluindo a solicitação pela exportação 2, e entrar na dashboard 17559 sem temporizador arbitrário.
- [x] Renderizar a dashboard 17559 com a fonte original integrada.
- [ ] Selecionar a versão desejada e lembrar a escolha na próxima inicialização.
- [ ] Carregar a recompilação, módulos, recursos e configuração correspondentes à versão selecionada.
- [ ] Associar a intro correta a cada conjunto, sem usar a da 17559 indiscriminadamente.
- [ ] Permitir início direto na dashboard para diagnóstico ou preferência do usuário.
- [ ] Reiniciar ou encerrar a plataforma de forma controlada em qualquer etapa.

**Aceite:** iniciar, chegar ao menu, fechar e repetir sem depender do estado de uma execução anterior.

## 2. Interface original, navegação e Guide — P0

- [ ] Navegar pelos menus e painéis locais da dashboard com controle, incluindo confirmar e voltar.
- [ ] Exibir textos, ícones, fontes, transições e animações corretamente, não apenas uma imagem estática.
- [ ] Abrir e fechar o Guide original na dashboard.
- [ ] Abrir e fechar o Guide original durante o jogo, permitindo acionar o pedido de retorno.
- [ ] Direcionar a entrada para a interface ativa, sem acionar simultaneamente o jogo por trás do Guide.
- [ ] Exibir e utilizar o teclado virtual original quando solicitado.
- [ ] Mostrar diálogos reais de confirmação, erros e notificações.
- [ ] Tratar telas dependentes de serviços indisponíveis sem travar nem informar conexão fictícia.

**Aceite:** navegação exercitada, entrada respondendo e ausência de telas vazias indevidamente classificadas como funcionais.

## 3. Perfis e contas locais — P0

Nome, identificador e estado de entrada precisam permanecer coerentes; as implementações de referência consultam esses dados por operações distintas. [R1]

- [ ] Iniciar sem usuário conectado e abrir o fluxo original de criação/seleção de perfil.
- [ ] Criar um perfil local persistente com nome e identificador estável pela interface original.
- [ ] Substituir o estado fixo de usuário, quando ainda presente, por seleção e entrada/saída reais; notificações devem refletir mudanças reais.
- [ ] Entrar, sair e trocar de perfil sem reiniciar a plataforma.
- [ ] Fazer dashboard e jogo enxergarem o mesmo perfil e o mesmo estado de entrada.
- [ ] Associar corretamente perfis aos jogadores/controles e suportar múltiplos usuários locais.
- [ ] Recuperar perfis e preferências após fechar e reabrir a plataforma.
- [ ] Manter saves e conquistas separados por usuário, sem misturar dados.
- [ ] Tratar exclusão, importação e migração de perfis com confirmação e preservação dos dados originais.

**Aceite:** dois perfis diferentes entram no mesmo jogo, preservam dados próprios e continuam corretos após reiniciar. Perfil local não deve ser apresentado como conta autenticada no Xbox Live.

## 4. Biblioteca e My Games — P0

- [x] Fazer o pedido de lançamento partir de My Games original na 17559: chamada relatada a `XamContentLaunchImageInternalEx` para `XSYSLAUNCH:\default.xex`.
- [ ] Adicionar jogos locais autorizados sem editar manualmente o código do launcher.
- [ ] Identificar título, versão, região e hash, sem confundir atualizações com jogos separados.
- [ ] Demonstrar a origem dos itens na enumeração real de conteúdo, não apenas uma entrada de demonstração do Castle Crashers.
- [ ] Mostrar nome, ícone/capa e detalhes a partir dos metadados disponíveis, sem entradas fictícias.
- [ ] Atualizar a listagem ao adicionar ou remover conteúdo, sem duplicações ou referências obsoletas.
- [ ] Distinguir jogo pronto, preparação necessária, dependência ausente e alvo ainda não suportado.
- [ ] Preservar a identidade do item selecionado até o pedido de lançamento; selecionar outro conteúdo deve mudar o destino resolvido.

**Aceite:** conteúdo realmente enumerado, selecionável e identificado de ponta a ponta. A chamada original já foi relatada; ela não comprova sozinha a correção geral do catálogo.

## 5. Lançamento original, saída e retorno — P0, trabalho atual

### 5.1. Resolver e iniciar o título selecionado

- [x] Executar Castle Crashers separadamente e chegar ao menu, conforme os relatos recebidos.
- [x] Conferir caminho e hash no supervisor na execução relatada; o tipo de hash e a associação completa ainda precisam de evidência.
- [ ] Demonstrar a cadeia conteúdo selecionado → XEX/versão correspondente → manifesto da recompilação → artefato nativo executado.
- [ ] Resolver `XSYSLAUNCH` para o conteúdo selecionado, sem associação fixa que faça qualquer pedido abrir Castle Crashers.
- [ ] Preservar caminho, flags e dados de lançamento aplicáveis, de acordo com o contrato da chamada realmente utilizada.
- [ ] Entregar ao jogo o contexto pertinente de perfil, controles, armazenamento e atualização selecionada.
- [ ] Verificar que XEX, configuração e artefato nativo pertencem ao mesmo alvo; conferir apenas o hash de um EXE não aprova essa associação.
- [ ] Recusar conteúdo inexistente, versão incompatível ou recompilação ausente com erro controlado, sem abrir outro jogo por padrão.
- [ ] Testar a resolução negativa com dados isolados, sem alterar os originais; um teste sintético não aprova outro jogo como compatível.
- [ ] Demonstrar a entrada no código do título correto e o gameplay após seleção na dashboard original.
- [ ] Evitar disputa entre dashboard e jogo por entrada, áudio, janela e apresentação gráfica.

**Aceite:** a seleção original, e não um atalho específico, determina qual jogo e versão começam a executar.

### 5.2. Atender ao pedido original de retorno

- [x] Atender `XamLoaderLaunchTitle(NULL, 0x100)` no ciclo Castle Crashers → 17559, com tradução do contexto Arcade baseada no XAM 17559; outros flags/versões não estão aprovados.
- [ ] **Em andamento:** distinguir pedido Xbox explícito, saída normal sem pedido, fechamento de janela e crash/encerramento forçado.
- [ ] Preservar destino, flags e dados aplicáveis antes da desmontagem do runtime, sem depender de memória destruída.
- [ ] Consumir cada pedido uma única vez e impedir reaproveitamento de pedidos de uma execução anterior.
- [ ] Manter o pedido explícito quando ele for seguido pelo encerramento normal do processo; a saída não deve apagá-lo.
- [x] Acionar o retorno pelo caminho original disponível no jogo: Castle Crashers chamou `XamLoaderLaunchTitle(NULL, 0x100)` na execução confirmada pelo usuário. Guide continua pendente.
- [ ] Retornar à mesma versão da dashboard com o perfil e o contexto pertinente preservados.
- [x] Não repetir a intro ao retornar do Castle Crashers para a 17559, confirmado pelo usuário e pelo log.
- [ ] Distinguir retorno ao menu de encerramento da plataforma.

**Aceite:** pedido real do jogo/Guide → encerramento controlado do título → dashboard correta, com contexto preservado e sem intro.

### 5.3. Fechamento da janela, falhas e repetição

- [ ] Tratar o fechamento de janela como ação do usuário no PC, sem registrá-lo como comando original do Xbox.
- [ ] Tratar saída normal sem pedido explícito segundo uma política do aplicativo identificada, sem inventar um pedido Xbox.
- [ ] Recuperar ou encerrar a plataforma após crash conforme a política definida, preservando saves/perfis e registrando o motivo real.
- [ ] Compartilhar limpeza de recursos quando apropriado, sem perder a identidade dos eventos.
- [ ] Testar retorno original, fechamento de janela, saída normal e falha separadamente.
- [ ] Repetir vários ciclos sem processos órfãos, pedidos antigos, controles presos ou crescimento indevido de recursos.
- [ ] Preservar a transição visual original entre dashboard e jogo; a janela persistente atual ainda apresenta corte seco relatado pelo usuário.

Diagnóstico da transição: a execução de 06:09 registrou callbacks originais de
encerramento que o runtime armazena mas não executa; VdPersistDisplay apareceu
somente na inicialização. O host perde a superfície entre títulos. A causa não
deve ser reduzida a “falta um fade”; contrato e sincronização ainda precisam de
correção verificada (`docs/dashboard.md` §0.19).

**Aceite do ciclo completo:** dashboard → seleção original → Castle Crashers → gameplay → save → retorno original → mesma dashboard → relançamento → carregamento do save. Fechar a janela ou abrir o jogo por comando externo não aprova esse ciclo.

## 6. Armazenamento e saves — P0

O VFS do SDK é a camada que relaciona caminhos do Xbox 360 a arquivos locais. A existência de um arquivo no PC não substitui sua disponibilidade no caminho solicitado pelo programa. [R2]

- [ ] Fornecer os dispositivos e caminhos efetivamente utilizados: conteúdo, dados do usuário, sistema, atualizações e cache.
- [ ] Permitir selecionar armazenamento quando o jogo ou a dashboard solicitar.
- [ ] Criar, ler, atualizar e excluir saves com identidade de usuário e título corretas.
- [ ] Fazer autosave e save manual persistirem após reiniciar jogo e plataforma.
- [ ] Informar espaço livre, dispositivo ausente, arquivo inexistente e erros de escrita corretamente.
- [ ] Manter dados graváveis separados dos executáveis e recursos originais preservados.
- [ ] Disponibilizar backup/restauração e proteger contra sobrescrita por versões incompatíveis.

**Aceite:** salvar progresso real, encerrar totalmente a plataforma, reabrir e continuar do ponto salvo.

## 7. Controles e uso no PC — P0/P1

- [ ] Suportar botões, direcionais, analógicos, gatilhos e vibração dos controles usados no projeto.
- [ ] Associar corretamente até quatro controles aos jogadores, conforme a funcionalidade do título.
- [ ] Detectar conexão, desconexão e reconexão sem perder permanentemente a entrada.
- [ ] Preservar mapeamentos e permitir teclado como alternativa quando configurado.
- [ ] Alternar janela/tela cheia, redimensionar e perder/recuperar foco sem quebrar a execução.
- [ ] Funcionar em caminhos com espaços, acentos e caracteres não ASCII.

**Aceite:** navegar e jogar sem usar o terminal durante o uso normal; desconectar e reconectar um controle sem reiniciar o jogo quando o título permitir.

## 8. Configurações e funções locais do sistema — P1

- [ ] Alterar idioma e região utilizando os recursos disponíveis para a versão, com persistência.
- [ ] Fornecer data, hora e fuso coerentes às interfaces e jogos.
- [ ] Aplicar opções de vídeo, proporção, áudio e controles efetivamente suportadas, sem configurações de fachada.
- [ ] Alterar temas, imagens de jogador e preferências visuais quando presentes na dashboard.
- [ ] Exibir cartão de jogador, histórico de jogos e conquistas locais reais.
- [ ] Registrar conquistas emitidas pelo jogo, persistir o progresso e mostrar as notificações correspondentes.
- [ ] Suportar avatares e sua edição nas versões em que existirem.
- [ ] Navegar no gerenciamento original de armazenamento e conteúdo.
- [ ] Reproduzir música, fotos e vídeos locais nas interfaces e formatos que forem suportados por cada versão.
- [ ] Implementar as opções de família/privacidade locais presentes, ou informar claramente as que ainda não têm efeito.

**Aceite:** uma configuração alterada produz efeito real e reaparece corretamente após reiniciar; funcionalidades ausentes não retornam sucesso fictício.

## 9. Todas as versões de dashboard — P1, meta permanente

- [x] Criar e executar o perfil separado da dashboard 6717 até o bloqueio relatado.
- [ ] Resolver o último bloqueio conhecido da 6717 (`0xAE6`) e demonstrar quadro real e navegação; nenhum resultado posterior foi apresentado.
- [ ] Demonstrar perfis, listagem e lançamento/retorno originais na 6717, preservando a 17559.
- [ ] Ampliar o suporte para as demais versões originais e betas inventariadas, sem exclusões silenciosas.
- [ ] Identificar e carregar os formatos antigos necessários; reconhecer uma assinatura não conta como executá-la.
- [x] Registrar os componentes inventariados da 6717 no manifesto `versions/6717/retail_system_assets/metadata/manifest.json`, conforme o relato; isso não comprova um conjunto completo nem assinatura validada.
- [ ] Manter manifestos equivalentes para as demais versões/hashes, separando componentes confirmados, pendências e formatos ainda não suportados.
- [ ] Respeitar os contratos de imports e módulos de cada alvo, sem assumir equivalência entre versões.
- [ ] Reutilizar o núcleo compartilhado, isolando correções específicas em perfis.
- [ ] Alternar versões sem misturar recursos nem corromper perfis, configurações ou saves.
- [ ] Registrar compatibilidade por combinação dashboard/jogo e testar as combinações disponíveis progressivamente.

**Aceite:** cada versão recebe resultados próprios para inicialização, renderização, navegação, perfis e integração com jogos. Uma versão aprovada não aprova as outras.

## 10. Preparação e compatibilidade dos jogos — P1, meta permanente

O ReXGlue traduz código PowerPC para C++ antecipadamente. A preparação de cada executável faz parte do produto; adicionar um XEX à pasta não produz automaticamente sua implementação nativa. [R3]

- [x] Demonstrar análise, geração, compilação e execução nos alvos já relatados; não equivale a suporte automático a qualquer XEX.
- [ ] Disponibilizar o fluxo de preparação como recurso geral e reproduzível, com diagnóstico por etapa.
- [ ] Reutilizar resultados válidos sem recompilar tudo a cada abertura e invalidar resultados incompatíveis.
- [ ] Suportar o conteúdo necessário aos jogos de disco, digitais/XBLA e demos do catálogo.
- [ ] Tratar revisões, regiões e atualizações com identificação suficiente para selecionar a recompilação correta.
- [ ] Aplicar Title Updates e disponibilizar DLC autorizado de forma compatível com o jogo e seu conteúdo.
- [ ] Atender jogos com vários discos, inclusive trocas durante a execução e instalações de conteúdo.
- [ ] Executar módulos adicionais solicitados pelo jogo mediante código preparado e registro de funções válido.
- [ ] Tratar títulos com múltiplos executáveis e transições internas, não somente seu primeiro menu.
- [ ] Validar gameplay prolongado, transições, vídeos, saves e os modos existentes em cada título.
- [ ] Registrar campanha concluída quando aplicável e resultados específicos para jogos sem campanha.

**Aceite:** “inicia”, “chega ao gameplay”, “jogável”, “concluído” e “funcionalidades verificadas” são estados distintos. Não declarar catálogo completo a partir de uma amostra.

O carregamento de um módulo e a disponibilidade de suas funções nativas são responsabilidades distintas no modelo do SDK. [R4]

## 11. Correção de execução, gráficos e áudio — P0/P1

O runtime documentado separa memória, funções do sistema, threads, arquivos, gráficos, áudio e entrada. A aprovação deve cobrir o comportamento em conjunto, não apenas o codegen. [R5]

- [ ] Executar corretamente os caminhos de CPU utilizados, incluindo chamadas indiretas, callbacks e tratamento de exceções.
- [ ] Manter contratos reais de APIs, memória, sincronização, eventos e temporizadores, sem retornos residuais ou sucesso arbitrário.
- [ ] Processar comandos gráficos e sinalizar conclusão somente quando o trabalho correspondente terminar.
- [ ] Renderizar geometria, texturas, transparência, iluminação, interface e vídeos conforme os casos testados.
- [ ] Reproduzir música, vozes e efeitos com mistura e sincronização corretas, sem cortes ou aceleração indevida.
- [ ] Preservar velocidade da lógica e animação independentemente do desempenho do PC.
- [ ] Inicializar e encerrar subsistemas na ordem correta, sem uso de memória ou dispositivos já destruídos.

**Aceite:** comparar cenas e comportamentos reproduzíveis com uma referência confiável. Uma imagem colorida ou um contador avançando não comprova correção.

## 12. Rede e periféricos especiais — P2

Estes são objetivos adicionais de cobertura, não pré-requisitos para concluir o primeiro ciclo offline. Dependências de serviços externos devem permanecer separadas do suporte local.

- [ ] Operar offline sem tentativas de conexão bloqueantes ou autenticação fictícia.
- [ ] Suportar System Link/LAN nos jogos que ofereçam esse modo, com descoberta e sessões reais.
- [ ] Implementar recursos online somente com um serviço compatível efetivamente disponível e validado.
- [ ] Distinguir perfil local, autenticação remota, amigos, mensagens e outros serviços; não tratar um como prova dos demais.
- [ ] Suportar Kinect e câmeras nos títulos dependentes, com entrada real ou adaptação funcional explicitamente identificada.
- [ ] Suportar microfones, instrumentos musicais, volantes e outros acessórios exigidos por títulos do catálogo.
- [ ] Tratar funcionalidades da dashboard dependentes de lojas, streaming ou servidores externos sem menus falsamente funcionais.

**Aceite:** um título dependente de periférico ou serviço continua com essa dependência registrada até que o modo correspondente funcione. Não pressupor conexão ao Xbox Live oficial.

## 13. Instalação, manutenção e proteção dos dados — P0/P1

- [ ] Oferecer um caminho reproduzível de instalação e execução no Windows x64.
- [ ] Separar preparação de conteúdo do uso normal: um alvo já preparado não deve exigir manipulação manual do CMake a cada abertura.
- [ ] Detectar recursos, ferramentas ou requisitos ausentes e informar o problema específico.
- [x] Manter o mecanismo relatado de patches locais do SDK com hashes, aplicação, verificação e reversão.
- [ ] Validar instalação/atualização completas em ambiente limpo, incluindo a distribuição correta da DLL utilizada.
- [ ] Preservar perfis e saves entre atualizações; rejeitar migrações incompatíveis de forma controlada.
- [ ] Limitar extração e escrita às pastas autorizadas, preservando arquivos pessoais e conteúdo original.
- [ ] Manter binários e recursos proprietários fora da distribuição e dos commits do projeto.

## 14. Critérios de validação e regressão — todas as etapas

- [x] Manter como referências relatadas: Iruka renderizando/animando, intro + dashboard 17559 e Castle Crashers chegando ao menu separadamente.
- [ ] Reexecutar as regressões relevantes após alterações compartilhadas, sem presumir que o estado antigo continua válido.
- [ ] Registrar versão/hash do alvo, revisão e patches do runtime, ambiente e teste realmente executado.
- [ ] Distinguir teste passou, teste falhou, ignorado, não executado e dependência ausente.
- [ ] Não converter falhas conhecidas, stubs ou hipóteses experimentais em sucesso silencioso.
- [ ] Avaliar desempenho por alvo e configuração, usando medidas, sem prometer um FPS universal.
- [ ] Validar repetição de lançamentos, retorno original, fechamento de janela, saída normal, crash e persistência como resultados separados.
- [ ] Manter a falha conhecida do teste `unit.Depfile escapes characters make treats as syntax` contada como falha enquanto não houver correção demonstrada.
- [ ] Exigir evidência posterior para marcar as duas correções de retorno anunciadas pelo Astra; intenção de corrigir não conta como teste aprovado.

## Marcos de entrega

| Marco | Resultado a demonstrar |
|---|---|
| M1 — experiência offline original | 17559 → perfil local → biblioteca original → Castle Crashers corretamente resolvido → gameplay → save → pedido original de retorno → mesma dashboard sem intro → carregar o save novamente. |
| M2 — plataforma multiversão | 6717 e 17559 utilizam a infraestrutura compartilhada com resultados separados e sem regressão. |
| M3 — catálogo crescente | Outros jogos e versões, com atualizações, módulos e modos efetivamente testados. |
| M4 — fidelidade ampliada | Funções locais adicionais da dashboard, rede e periféricos conforme sua disponibilidade e validação. |

## Regra de trabalho para os agentes

Este arquivo é um mapa de cobertura, não uma ordem para implementar tudo numa sessão. Escolher um bloqueio funcional, reproduzir, corrigir, testar e executar novamente. Reutilizar ferramentas e testes existentes; atualizar apenas os itens sustentados pelo resultado. Não criar infraestrutura ou documentação extra só para preencher o checklist. As correções locais pequenas, reversíveis e testadas continuam dentro da autonomia já concedida.

## Referências técnicas

As referências explicam arquitetura e contratos; não comprovam a implementação atual do projeto local. Confirmar APIs na revisão fixada antes de programar. O estado marcado como relatado vem da conversa, não de inspeção local.

- [R1] Xenia Canary — consultas de perfil e estado: https://github.com/xenia-canary/xenia-canary/blob/canary_experimental/src/xenia/kernel/xam/xam_user.cc
- [R2] ReXGlue — Virtual File System: https://github.com/rexglue/rexglue-sdk/wiki/Virtual-File-System
- [R3] ReXGlue — modelo AOT: https://github.com/rexglue/rexglue-sdk
- [R4] ReXGlue — Kernel State & Objects: https://github.com/rexglue/rexglue-sdk/wiki/Kernel-State-%26-Objects
- [R5] ReXGlue — Runtime Architecture Overview: https://github.com/rexglue/rexglue-sdk/wiki/Runtime-Architecture-Overview
