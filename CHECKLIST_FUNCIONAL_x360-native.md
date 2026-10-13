# Checklist funcional — x360-native

**Objetivo:** escolher uma dashboard original → executar a intro correspondente → entrar em um perfil → listar e selecionar um jogo pela interface original → atender ao pedido original de lançamento → jogar → salvar → atender ao pedido original de retorno → voltar à mesma dashboard, sem repetir a intro.

Windows x86-64 é o primeiro alvo funcional relatado, não uma restrição permanente. Windows, Linux e macOS são objetivos, com estado separado por sistema e arquitetura. Recompilação estática com ReXGlue; suporte progressivo a todas as versões originais de dashboard, incluindo antigas e betas, e ao catálogo de jogos Xbox 360.

**Objetivo de distribuição:** aplicativo pronto nas Releases do GitHub do proprietário do projeto. Uma pessoa sem conhecimento de programação deve conseguir baixar → extrair/instalar → abrir → seguir o assistente → fornecer conteúdo autorizado → preparar → entrar na dashboard → selecionar um jogo → jogar → salvar → voltar à dashboard. Facilidade de uso não significa compatibilidade universal; requisitos e limitações precisam ser claros antes da preparação.

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
Incremento §0.20: callbacks de encerramento/preparação restaurados e troca do ring
buffer sincronizada. Ciclo 17559 → Castle Crashers → 17559 sem intro repetido;
32/32 sdkcheck e 40/40 x360n passaram. **Corte e estalo de áudio continuam pendentes**:
o pedido VdPersistDisplay agora chega, mas sua preservação e a apresentação do XAM
ainda não são atendidas. Não marcar fidelidade da transição como concluída.
Incremento §0.21: imagem convidada preservada e sequência de gamma do XAM 17559
implementada no perfil, usando rotinas D3D originais recompiladas. Após corrigir
a drenagem do áudio, ciclo completo repetido sem intro no retorno; usuário
confirmou entrada recuperada e ausência de cortes. 35/35 sdkcheck e 40/40
supervisor passam. Resultado limitado a 17559/Castle Crashers/D3D12 Windows;
sem comparação audiovisual medida contra console nem aprovação das outras versões.
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

Diagnóstico 2026-10-12 (§0.22): Settings chama o cartão de jogador. O pedido
FE/21008 agora preserva os argumentos e retorna falha real (1627), pois o serviço
da interface original ainda falta. Seleção não emite mais notificações fictícias;
User continua fixo no SDK. Entrada/saída/criação/persistência permanecem pendentes.
O ciclo com Castle Crashers foi repetido, com retorno sem intro.

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
- [ ] Preservar a transição visual original entre dashboard e jogo; ausência de cortes confirmada no ciclo 17559/Castle Crashers após §0.21; fidelidade geral e outras versões ainda pendentes.

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
- [ ] Suportar avatares e sua edição nas versões em que existirem. Em 2026-10-13,
  serviço original 17559 isolado levou Motocross ao tutorial; chão preto e piloto
  deformado ainda impedem aprovação visual. Edição/persistência não validadas
  ([evidência](docs/avatar-system.md#14-servico-real-e-entrada-no-tutorial-2026-10-13)).
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

## 15. Aplicativo e distribuição multiplataforma — P0/P1

Os requisitos das seções 15–19 foram incorporados em 2026-10-12. A publicação privada e a reconciliação do histórico (§19) foram concluídas. Os §§15–17 têm candidato ZIP Windows x64, importação, ferramentas gerenciadas e preparação gráfica. O pacote carrega suas fontes e obtém dependências com consentimento; primeira preparação completa em máquina limpa e aceite do §18 seguem pendentes. Não confundir o candidato local com uma release aprovada. Backend e SDK permanecem os mesmos.

Referência de facilidade de uso: [Pinyon Shift](https://github.com/arcanite24/pinyon-shift#play), com download do launcher, importação e preparação guiada descritos no README. Usar a experiência como referência, sem copiar a interface ou substituir nossa arquitetura para se parecer com ela. Um ZIP de código-fonte com instruções para montar um ambiente de desenvolvimento não satisfaz este requisito.

### 15.1. Estado por sistema e arquitetura

| Sistema | Arquitetura candidata | Evidência funcional atual neste checklist | Pacote real e aceite para leigos |
|---|---|---|---|
| Windows | x86-64 | Execuções locais relatadas da 17559, intro e Castle Crashers, com os limites acima. | Pendente. |
| Windows | ARM64 | Viabilidade, implementação e execução não demonstradas. | Pendente; não anunciado como suportado. |
| Linux | x86-64 | Execução funcional não demonstrada. | Pendente; não anunciado como suportado. |
| Linux | ARM64 | Viabilidade, implementação e execução não demonstradas. | Pendente; não anunciado como suportado. |
| macOS | x86-64 | Viabilidade, implementação e execução não demonstradas. | Pendente; não anunciado como suportado. |
| macOS | ARM64 | Viabilidade, implementação e execução não demonstradas. | Pendente; não anunciado como suportado. |

Arquiteturas candidatas só se tornam alvos suportados quando viáveis, implementadas e testadas. Um preset ou build bem-sucedido não comprova suporte funcional. Registrar por combinação: interface, preparação, gráficos, áudio, controles, armazenamento, lançamento, retorno e atualização, com versões e limitações.

- [ ] Preservar o núcleo compartilhado e isolar diferenças de sistema, implementando portabilidade progressivamente sem perder os resultados existentes.
- [ ] Oferecer um caminho para os demais alvos sem tornar o fluxo normal exclusivamente dependente de PowerShell, Win32 ou D3D12; manter o backend validado durante a evolução.
- [ ] Anunciar suporte somente para combinações de sistema/arquitetura com evidência funcional e aceite do pacote real.

### 15.2. Pacote principal e conteúdo distribuível

O requisito é **um download principal por sistema/arquitetura suportados**, não um executável universal. Exemplos de apresentação: ZIP com aplicativo principal no Windows, pacote de uso simples no Linux e `.app` em ZIP ou pacote adequado no macOS. Não exigir que o usuário escolha e combine downloads de DLLs, ferramentas e componentes internos.

- [x] Identificar claramente o pacote principal por versão, sistema e arquitetura — candidato local `x360-native-0.1.0-preview.*-windows-x64.zip`, com commit e hashes no manifesto.
- [x] Oferecer aplicativo principal com nome e ícone claros — `x360-native.exe`, ícone próprio XN e metadados de versão.
- [x] Abrir a interface gráfica sem terminal no uso normal — demonstrado na prévia local Windows `x360-native.exe`; preparação e supervisor usam processos sem console. Distribuição autônoma ainda pendente.
- [x] Incluir dependências redistribuíveis ou obtê-las pelo assistente quando necessário e permitido — CRT Release incluído; Git/CMake/Ninja/LLVM/SDK fixados preparados localmente; instalador oficial C++ verificado e oferecido com consentimento. Instalação de MSVC ausente ainda não executada.
- [ ] Eliminar dependência de caminhos, arquivos ou ferramentas presentes apenas no computador do desenvolvedor.
- [x] Dar instruções curtas sobre qual pacote baixar e qual aplicativo abrir — README do projeto e READ-ME do candidato.
- [x] Distinguir o aplicativo pronto dos arquivos de código-fonte oferecidos pelo GitHub — README e interface explicitam que não há pacote autônomo aprovado.
- [ ] Apresentar requisitos mínimos e limitações da release antes de iniciar preparação demorada.
- [ ] Distribuir apenas o aplicativo e componentes com redistribuição permitida; verificar as autorizações aplicáveis.
- [x] Excluir dashboards, jogos, fontes proprietárias, XAM, bootanim e código derivado desses binários quando não houver autorização aplicável — empacotamento recusa essas extensões, `.local`, dependências locais e AOT gerado. Recompilação para código nativo não concede direito de redistribuição.
- [x] Explicar no assistente os arquivos externos necessários e aceitar conteúdo autorizado somente nos formatos efetivamente suportados, sem esconder a primeira preparação — conjuntos extraídos declarados; STFS/ISO na interface seguem indisponíveis.

## 16. Primeira execução e preparação guiadas — P0

### 16.1. Assistente e importação

- [x] Abrir a interface mesmo sem jogos ou dashboard preparados — janela executada também contra ambiente vazio, com ação de selecionar ambiente e lançamento desabilitado.
- [ ] Verificar sistema, arquitetura, recursos gráficos, espaço, permissões e dependências relevantes.
- [ ] Oferecer mensagens compreensíveis; PT-BR e inglês são os idiomas iniciais planejados.
- [x] Permitir seleção visual das pastas de conteúdo e dados — ambiente e armazenamento persistidos; mudar seleção não move nem apaga saves antigos.
- [x] Importar por seleção de arquivos/pastas e, quando apropriado, arrastar e soltar — arquivos individuais e pastas extraídas, com importação incremental; arquivo e pasta da 17559 testados pela GUI do ZIP independente. Biblioteca vazia não exibe títulos apenas cadastrados. Handler de arrastar/soltar implementado. STFS pelo assistente e outros conjuntos continuam pendentes.
- [x] Identificar automaticamente versões e componentes — identidade XEX nos perfis conhecidos; pasta contendo várias versões é recusada como ambígua. Não identifica todo o acervo.
- [ ] Dispensar edição manual de TOML/JSON, renomeação de arquivos e montagem manual da estrutura de pastas.
- [x] Informar arquivo ausente, incompatível ou corrompido com ação concreta para prosseguir — componentes dos perfis de preparação são conferidos por SHA-256; DLL ausente e preparação interrompida impedem estado pronto. Isso não valida todos os assets de um jogo.
- [x] Preservar arquivos originais e conjuntos funcionais durante a importação e preparação — hashes antes da escrita, publicação sem substituição e teste de retomada; conteúdo instalado divergente é preservado.
- [x] Impedir mistura silenciosa de componentes de dashboards diferentes para completar conjuntos — importador aceita somente hashes declarados no perfil e recusa destino existente diferente. A 6717 não recebe componentes da 17559.
- [ ] Mostrar o próximo passo em linguagem simples e reservar stack traces e informações técnicas para “Ver detalhes”.

Reutilizar os arquivos, inventários e downloads já obtidos no projeto; não reiniciar buscas para preencher estes requisitos.

### 16.2. Preparação automatizada

- [x] Iniciar e acompanhar a preparação pela interface — scripts sem terminal e logs locais; preview.13 executou importação, downloads, geração e compilação do zero da 17559 fora do repositório, com intro/menu e textos conferidos. MSVC/Windows SDK já instalados; Windows limpo permanece pendente.
  - Extensão executada no preview.18: pasta extraída de Castle Crashers 0.0.1.3 importada (708 arquivos), compilada no ambiente externo e registrada na My Games original. Ciclo dashboard → jogo → retorno solicitado pelo jogo confirmado, mesma janela e sem repetir intro. Caminhos longos e estado pronto após registro parcial corrigidos; navegação das abas não repete os hashes. STFS/ISO, outros títulos e Windows limpo não estão aprovados por esse ensaio.
- [ ] Dispensar configuração manual de Git, CMake, compilador, Python ou SDK pelo usuário.
- [x] Gerenciar ferramentas indispensáveis pelo aplicativo, com fontes verificáveis e versões controladas — arquivos oficiais com hashes fixados; checkout e patches do SDK conferidos.
- [x] Informar e obter consentimento quando instalação, download relevante ou elevação de privilégios forem necessários — downloads recusados sem consentimento; instalação interativa Microsoft separada e explicitada.
- [x] Preferir funcionamento sem privilégios administrativos — aplicativo, importação e ferramentas locais; instalador C++ do sistema pode exigir elevação informada.
- [ ] Informar espaço necessário e downloads antes de começar.
- [x] Exibir progresso por etapas reais, sem porcentagens inventadas — mensagens `X360N_STAGE` de download, verificação, configure, codegen e build.
- [ ] Distinguir trabalho em andamento, espera e erro.
- [ ] Permitir cancelamento seguro e recuperação após interrupção.
- [x] Reaproveitar etapas válidas, reconstruindo somente o que precisa mudar — cache de ferramentas, patches idempotentes e build incremental executados; saída interrompida não é aceita como pronta.
- [x] Evitar recompilação desnecessária a cada abertura — abrir dashboard usa a sessão já preparada; preparar é uma ação separada e explícita.
- [ ] Permitir uso offline do conteúdo preparado nas funcionalidades independentes de serviços externos.

Ferramentas podem executar internamente; instalação, configuração e diagnóstico da cadeia de desenvolvimento não devem ser transferidos ao usuário leigo.

## 17. Uso normal, recuperação e dados — P0/P1

### 17.1. Interface e limites do assistente externo

- [x] Selecionar a dashboard por uma interface compreensível — catálogo declarativo por alvo; 17559 habilita sessão, 6717 aparece com limites e sem sessão guiada anunciada.
- [x] Mostrar estados claros: pronto, precisa preparar, falta componente e ainda não suportado — limitados aos perfis de preparação cadastrados. Importação/execução indisponíveis permanecem desabilitadas.
- [ ] Fornecer padrões seguros e separar configurações avançadas.
- [ ] Manter textos legíveis e escala adequada à tela.
- [ ] Permitir navegação por teclado e controle nas telas aplicáveis.
- [ ] Suportar espaços, acentos e caracteres não ASCII nos caminhos em todo o fluxo.
- [ ] Tratar corretamente fechamento, perda de foco e retomada.
- [ ] Oferecer recuperação sem perder perfis ou saves.

O assistente externo cuida de instalação, preparação, escolha do ambiente e recuperação. Ele não substitui a dashboard original, My Games, Guide ou o fluxo de perfis por telas semelhantes para esconder funções ausentes. Lançamento e retorno continuam sujeitos às solicitações originais e aos critérios da seção 5.

### 17.2. Erros, atualização e privacidade

- [ ] Explicar erros em linguagem simples e oferecer ações pertinentes: tentar novamente, selecionar arquivos ou abrir diagnóstico.
- [x] Permitir revisar relatórios de diagnóstico antes de compartilhar — detalhes e logs locais, sem ação automática de compartilhamento.
- [x] Não enviar automaticamente conteúdo, saves, credenciais ou informações pessoais — preparação e supervisor não têm upload/telemetria.
- [ ] Guiar atualizações preservando perfis, saves, conteúdo e preparações ainda compatíveis.
- [ ] Recuperar atualizações interrompidas e permitir retornar a uma versão anterior quando viável.
- [x] Informar claramente onde os dados ficam armazenados — interface mostra a pasta do perfil e oferece abrir dados locais; detalhes e registros ficam em `.local/logs/desktop/` e `.local/runs/`.
- [x] Remover o aplicativo sem apagar dados pessoais sem escolha explícita — pacote portátil separado do ambiente; remoção do pacote mantém conteúdo e dados, instrução incluída.
- [ ] Tratar verificações de segurança de cada sistema sem instruir a desativar antivírus ou proteções como procedimento normal.

Não criar agora serviços de atualização, telemetria ou ferramentas apenas para preencher o checklist.

## 18. Aceite do pacote para usuários leigos — bloqueador da release

Usar o **pacote real da release** em cada sistema/arquitetura anunciado como suportado, em computador com sistema e hardware suportados, usuário comum, sem o repositório e sem ambiente de desenvolvimento previamente configurado. A árvore do desenvolvedor não substitui este teste.

- [ ] Demonstrar o fluxo completo: baixar o pacote correto → abrir → importar conjunto suportado → concluir preparação guiada → entrar na dashboard → usar o perfil → selecionar jogo validado → jogar e salvar → retornar pelo fluxo original → fechar → reabrir e recuperar progresso.
- [ ] Testar dependência ausente, com diagnóstico e recuperação compreensíveis.
- [ ] Testar espaço insuficiente sem comprometer originais ou dados existentes.
- [ ] Testar caminho com espaços e acentos.
- [ ] Interromper e recuperar a preparação com segurança.
- [ ] Atualizar com dados existentes e verificar sua preservação.
- [ ] Registrar cada intervenção técnica necessária; enquanto o fluxo normal exigir um desenvolvedor, este aceite permanece pendente.
- [ ] Validar e registrar por sistema/arquitetura: interface, preparação, gráficos, áudio, controles, armazenamento, lançamento, retorno e atualização.

O aceite comprova facilidade de uso para o conjunto e o ambiente testados, não compatibilidade universal nem todos os jogos na primeira release.

**Incremento gráfico de 2026-10-12:** candidato local e ferramentas gerenciadas implementados; importação de conteúdo completo e registro do catálogo SDK por dashboard. Testes e limites em `docs/desktop-preparation.md`. Caminhos Unicode foram testados na importação e no runtime iruka; isso não equivale ao fluxo completo em máquina limpa. Perfis originais continuam bloqueados no host XUI de atualização/composição/entrada. Save/load e atualização/rollback não aprovados. Todos os itens do §18 permanecem abertos até teste do pacote nas condições exigidas.

## 19. Commits e preparação da primeira publicação — bloqueador obrigatório

### 19.1. Regra vigente para novos commits

Todos os novos commits próprios devem usar **Conventional Commits**: `tipo(escopo): descrição objetiva da mudança`, com tipo conforme o conteúdo real (`feat`, `fix`, `refactor`, `perf`, `test`, `docs`, `build`, `ci`, `chore`). Exemplos: `fix(launcher): preserve dashboard return requests` e `docs(checklist): add cross-platform usability requirements`.

Títulos, corpos e rodapés não devem atribuir autoria a Claude, Codex, Astra ou outros assistentes, nem incluir “Generated with...”, “Implemented by...” ou `Co-Authored-By` de assistente. Preservar créditos humanos, licenças, avisos de terceiros e referências técnicas necessárias ao upstream. Não apagar instruções de agentes nem falsificar autoria de terceiros. Regra persistente também em `CLAUDE.md`.

### 19.2. Revisão e reescrita antes do primeiro push

**A primeira publicação exigiu revisar e reescrever as mensagens de TODOS os commits locais próprios publicados**, aplicando a regra acima — não apenas os mais recentes. Este portão foi cumprido em 2026-10-12. A preparação ocorreu em cópia isolada, preservando a árvore ativa; a reconciliação posterior ocorreu após confirmação de exclusividade. Qualquer nova reescrita continua exigindo exclusividade e preservação do trabalho local.

- [x] Identificar todo o intervalo próprio ainda não publicado e verificar se há commits já compartilhados.
- [x] Preservar backup recuperável e verificável fora do conjunto que será publicado.
- [x] Preservar separadamente alterações não commitadas e arquivos não rastreados necessários, sem limpeza destrutiva.
- [x] Reescrever mensagens com base no conteúdo real de cada commit.
- [x] Preservar commits úteis, sem reduzir tudo a um “initial commit” para esconder o histórico.
- [x] Preservar conteúdo, autores humanos e datas, sem falsificar resultados nem atribuir trabalho de terceiros ao projeto.
- [x] Não reescrever o histórico upstream das dependências.
- [x] Validar títulos, corpos e rodapés de todo o intervalo próprio.
- [x] Conferir que a reescrita exclusivamente de mensagens preservou os snapshots correspondentes.
- [x] Manter localmente o mapeamento entre hashes antigos e novos.
- [x] Atualizar referências internas afetadas de forma controlada, sem alterar hashes de dependências externas por engano.
- [x] Auditar todo o histórico a publicar, não só o último commit, para excluir credenciais, dados pessoais, conteúdo proprietário e demais arquivos indevidos.
- [x] Não publicar backups ou referências que reincorporem histórico anterior sem revisão.
- [ ] Criar tags de release somente sobre o histórico final validado. Não se aplica a esta publicação privada de fontes; nenhuma release/tag foi criada.

Se houver commits já compartilhados, informar a situação antes de alterar esse histórico; não executar force-push automaticamente. Preparar o histórico não autoriza push, publicação de release ou mudança de visibilidade do repositório.

**Portões de publicação:** revisão/reescrita e auditoria de todo o histórico próprio antes do primeiro push; conteúdo distribuível autorizado e aceite do pacote real (§15 e §18) antes de anunciar a release para usuários. A revisão de mensagens e a auditoria do candidato privado foram concluídas em 2026-10-12. Os portões de distribuição e aceite do pacote continuam pendentes; esta publicação de fontes não anuncia uma release funcional.

### 19.3. Publicação privada e reconciliação concluídas — 2026-10-12

- Destino autorizado: `fabegalo/x360-native`, privado até nova instrução do proprietário.
- Base publicada: último incremento consolidado da cena XUI; alterações concorrentes não commitadas foram preservadas localmente. Não reescrever a árvore ativa enquanto outra sessão trabalha.
- Intervalo revisado: 32 commits próprios, desde a raiz. Antes da primeira publicação não havia remoto, branches remotas ou tags no repositório de origem; nenhum histórico upstream foi reescrito.
- Backup recuperado em cópia isolada e verificado por `git bundle verify` e `git fsck`. Bundle original, patches das alterações locais, cópias dos arquivos em andamento, auditoria e mapa antigo→novo permanecem fora do conjunto publicado, em `.local/publication/20261012-private/`.
- Reescrita exclusiva de mensagens: 32/32 árvores, autores e datas idênticos. Títulos Conventional Commits; removidos os rodapés de autoria de assistente. Créditos humanos e referências técnicas preservados.
- Auditoria de todos os snapshots: 422 blobs, 182 caminhos históricos; sem binários, recursos proprietários, conteúdo de `.local`, fontes AOT geradas ou padrões de credenciais detectados. Metadados do autor humano foram preservados conforme §19.2. Isto não constitui garantia universal de ausência de segredos.
- SDK permanece gitlink público fixado em `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`; o histórico da dependência não integra a reescrita. Atualizadas apenas duas referências internas ao commit próprio do primeiro quadro.
- Publicada somente a branch `main` revisada: 32 commits reescritos e o registro da auditoria, até `b31aae906cd636fb793cc965d7dfbc9e1d5d1813`. Sem push de backup, espelhamento, force-push, tags ou release. Visibilidade `PRIVATE` reconfirmada pela API do GitHub.
- Árvore de desenvolvimento reconciliada após o usuário confirmar que a outra sessão parou: `main` passou a acompanhar `origin/main`. Conferidos HEAD, diferenças locais, arquivos em andamento e SDK preservados. Evidência local em `.local/publication/20261012-private/reconciliation-1791814020853/after.json`; publicação em `published-verification.json`. Commits posteriores não significam envio automático nem mudança de visibilidade.

## 20. Títulos XNA / Xbox Live Indie Games — P1/P2

Os títulos XNA devem ser tratados como uma categoria diferente dos executáveis nativos recompilados pelo ReXGlue.

- [ ] Detectar automaticamente títulos XNA e diferenciá-los dos jogos nativos de Xbox 360.
- [ ] Identificar a versão de XNA Framework e do runtime gerenciado exigida pelo título.
- [ ] Carregar e executar assemblies gerenciados do jogo sem tratá-los como código PowerPC nativo comum.
- [ ] Implementar ou integrar um ambiente compatível com o .NET Compact Framework usado pelo Xbox 360.
- [ ] Implementar as APIs necessárias de Microsoft.Xna.Framework, incluindo ciclo Game.Initialize / Update / Draw.
- [ ] Implementar GraphicsDevice e os serviços gráficos utilizados pelos títulos XNA sobre os backends do projeto.
- [ ] Suportar o XNA Content Pipeline em runtime: localizar e carregar os assets compilados usados pelo jogo.
- [ ] Suportar áudio, entrada, armazenamento e networking usados pelas APIs XNA, integrados com os serviços compartilhados do x360-native.
- [ ] Mapear gamer services, profiles, storage e Guide chamados através das APIs XNA para o mesmo estado utilizado pela dashboard e pelos jogos nativos.
- [ ] Preservar comportamento de threads e sincronização relevante do .NET Compact Framework do Xbox 360.
- [ ] Validar exceções, garbage collection e semântica do runtime gerenciado nos caminhos realmente utilizados pelos jogos.
- [ ] Lançar títulos XNA a partir da My Games original usando a mesma identidade de conteúdo e fluxo original da dashboard.
- [ ] Retornar corretamente do título XNA à dashboard, sem criar um launcher paralelo específico para XNA.
- [ ] Persistir saves, configurações e perfil utilizando a mesma infraestrutura de usuário da plataforma.
- [ ] Registrar compatibilidade XNA separadamente: identificado → runtime carregado → inicia → menu → gameplay → save/load → jogável.
- [ ] Testar diferentes gerações de XNA, sem assumir que um jogo funcionando valida todas as versões do framework.

**Critério de aceite:** dashboard original → My Games → selecionar título XNA → runtime gerenciado inicia o jogo → gráficos/áudio/entrada funcionam → save persiste → retorno original à dashboard.

Não classificar um título XNA como compatível apenas porque seus arquivos ou assemblies puderam ser extraídos.

## Marcos de entrega

| Marco | Resultado a demonstrar |
|---|---|
| M1 — experiência offline original | 17559 → perfil local → biblioteca original → Castle Crashers corretamente resolvido → gameplay → save → pedido original de retorno → mesma dashboard sem intro → carregar o save novamente. |
| M2 — plataforma multiversão | 6717 e 17559 utilizam a infraestrutura compartilhada com resultados separados e sem regressão. |
| M3 — catálogo crescente | Outros jogos e versões, com atualizações, módulos e modos efetivamente testados. |
| M4 — fidelidade ampliada | Funções locais adicionais da dashboard, rede e periféricos conforme sua disponibilidade e validação. |
| M5 — distribuição utilizável | Pacote real aprovado pelo fluxo da seção 18 para cada sistema/arquitetura anunciado, com conteúdo distribuível autorizado e histórico final revisado. |

## Regra de trabalho para os agentes

Este arquivo é um mapa de cobertura, não uma ordem para implementar tudo numa sessão. Escolher um bloqueio funcional, reproduzir, corrigir, testar e executar novamente. Reutilizar ferramentas e testes existentes; atualizar apenas os itens sustentados pelo resultado. Não criar infraestrutura ou documentação extra só para preencher o checklist. As correções locais pequenas, reversíveis e testadas continuam dentro da autonomia já concedida.

## Referências técnicas

As referências explicam arquitetura e contratos; não comprovam a implementação atual do projeto local. Confirmar APIs na revisão fixada antes de programar. O estado marcado como relatado vem da conversa, não de inspeção local.

- [R1] Xenia Canary — consultas de perfil e estado: https://github.com/xenia-canary/xenia-canary/blob/canary_experimental/src/xenia/kernel/xam/xam_user.cc
- [R2] ReXGlue — Virtual File System: https://github.com/rexglue/rexglue-sdk/wiki/Virtual-File-System
- [R3] ReXGlue — modelo AOT: https://github.com/rexglue/rexglue-sdk
- [R4] ReXGlue — Kernel State & Objects: https://github.com/rexglue/rexglue-sdk/wiki/Kernel-State-%26-Objects
- [R5] ReXGlue — Runtime Architecture Overview: https://github.com/rexglue/rexglue-sdk/wiki/Runtime-Architecture-Overview
