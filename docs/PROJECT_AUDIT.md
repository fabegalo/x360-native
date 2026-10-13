# Auditoria técnica do x360-native

Data: 13/10/2026. Base auditada: `main`, commit `55e326c77fd4a26f44d69468ea67ab1670e3c4cd`.

**Atualização posterior à auditoria, 13/10/2026:** C1 e C3 foram corrigidos e validados no incremento de extração. O restante do relatório conserva o diagnóstico da base acima. Implementação, testes antes/depois e limites estão em E6; os demais achados continuam abertos.

Este relatório descreve o código nessa revisão, testes executados nesta auditoria e evidências históricas, identificadas separadamente. Não é uma declaração de compatibilidade geral. Nenhuma funcionalidade, patch de runtime ou correção de comportamento foi implementada durante a auditoria.

## A. Veredito geral

**A direção arquitetural é adequada; o produto ainda não está pronto para uma Release pública apresentada como console funcional.** AOT por identidade exata do conteúdo, runtime compartilhado, bindings específicos por dashboard e supervisor separado são escolhas coerentes com dashboards originais e crescimento gradual. Não há evidência que justifique trocar linguagem, SDK, framework ou modelo de execução.

A base suporta adicionar títulos, mas a operação de centenas de títulos exige corrigir a identidade dos artefatos preparados, a invalidação do código gerado e os diretórios compartilhados de saída do SDK. Hoje, reconhecer o XEX correto não garante que o executável e suas DLLs tenham sido produzidos para aquele conjunto de fontes e patches. Este é o maior risco de manutenção: diagnosticar como incompatibilidade de jogo uma mistura de artefatos antigos.

Também existem problemas concretos anteriores à expansão do catálogo: um ISO sintético malformado derruba o extrator antes das verificações de limites da aplicação; o manifesto de extração pode sobrescrever um arquivo existente; perfis, Guide e saves por usuário continuam incompletos; a atualização do aplicativo pode reutilizar silenciosamente um ambiente antigo. A suíte completa não está verde.

O serviço de avatares representa progresso real: nesta auditoria, o gerador original passou no probe e o backend habilitado completou `Initialize`, montagem de assets e mipmaps em Motocross. Isso não comprova animação, fidelidade visual ou suporte a Keflings. Da mesma forma, o ciclo original dashboard–Castle–dashboard possui evidência histórica específica, mas não equivale a um Guide completo.

### Consolidação na main e preservação do trabalho

- `main` e a referência local `origin/main` apontavam para a revisão acima. Não houve consulta remota nem push.
- Os commits de distribuição `2b4b9b8`, de avatares `1a4608a` e a integração `5ee0f3d` são ancestrais de `main`. Nenhum commit alcançável pelas referências locais ficou fora de `main` em `git log --all --not main`.
- A worktree de distribuição remanescente estava limpa, em HEAD destacado `2b4b9b8`. Sua existência não significa código pendente de integração. A preservação da antiga worktree de avatares está documentada em `docs/avatar-system.md`, seção 15.
- Alterações preexistentes: submódulo `external/rexglue-sdk` modificado e `scripts/window-route.ps1` modificado. Foram preservadas. O SHA-256 deste script é `c307f4bac6ec049967c445d7672c4357d009419303a372a77ae10cba67863201`.
- O único entregável versionável produzido é este relatório. Logs, fixtures sintéticas, builds e extração do ZIP ficaram em áreas locais ignoradas ou temporárias. Não houve reset, clean, rebase, commit ou publicação.

### Separação de responsabilidades observada

| Área | Implementação observada | Avaliação |
|---|---|---|
| Runtime, kernel, memória, threads, VFS, GPU, áudio e entrada | `external/rexglue-sdk`, com patches fixados em `deps.lock.json` | Centralização correta. Ampliar contratos com testes, evitando comportamento por nome de jogo no SDK. |
| Dashboards | `titles/dash_17559`, `titles/dash_6717`, manifests, analysis e bindings por versão | Separação correta. 17559 não deve fornecer automaticamente fontes, endereços ou módulos para 6717. |
| Jogos | `titles/iruka`, `titles/castle_crashers`, `titles/motocross_madness` | Identidade exata do XEX e ajustes locais são adequados; preparação ainda não é um pipeline robusto de catálogo amplo. |
| Lançamento | `titles/common/launch_handoff.*`, `src/supervisor/session_profile.cpp`, `main.cpp` | Contrato compartilhado, revalidação por transição e processo por título são bons limites. |
| Avatares | `titles/common/avatar/*` e serviço em `titles/motocross_madness/src/avatar_service.cpp` | Parsers/lifecycle compartilhados; backend AOT contém endereços exclusivos de XAM 17559. Ainda não é serviço universal. |
| Perfis e interfaces do sistema | SDK HLE mais binding XUI da 17559 | Incompleto. Carregar/criar uma cena não entrega composição, input ou persistência de usuário. |
| Aplicativo | Modelo em `src/desktop/{catalog,library,workspace,identify}.cpp`; UI em `main_win32.cpp` | Separação testável. O launcher conserva a dashboard original como experiência de console. |
| Supervisor | `src/supervisor`, núcleo independente de `rexruntime` | Boa decisão: diagnostica inclusive falhas do runtime sem precisar carregá-lo no próprio processo. |
| Distribuição | `package-desktop.ps1`, `prepare-tools.ps1`, payload com hashes | Base utilizável, mas atualização, vínculo fonte/binário e validação em Windows limpo ainda faltam. |

Não identifiquei dependência circular impeditiva no grafo compilado, nem uma camada sem finalidade que mereça remoção. Há repetição concreta de configuração gerada/CMake por título e de preparação de globais XAM entre bindings. Ela deve ser reduzida quando houver um segundo consumidor com o mesmo contrato; criar agora uma hierarquia universal de dashboards ou avatares esconderia diferenças reais.

## B. Estado real dos fluxos

Legenda: **testado agora** exige evidência obtida nesta auditoria; **histórico** identifica validação anterior descrita nos documentos, sem promovê-la a nova execução. Smoke por tempo limitado comprova apenas o comportamento explicitamente registrado.

| Fluxo | Estado | Evidência e limite |
|---|---|---|
| Identificação XEX2 e correspondência exata do conteúdo | Implementado e testado agora | Testes de identidade, manifests e catálogo passaram. XEX1 e algumas entradas inválidas são inventariadas, mas não são preparáveis pelo fluxo atual. |
| Dashboard 17559: intro original e entrada | Parcialmente comprovado agora | Log registra a sequência de exports da intro, conclusão e entrada da dashboard; timeout de 45 s. Não houve nova confirmação visual/sonora completa. |
| Dashboard 17559: renderização, fontes e navegação | Implementado com validação histórica | `docs/dashboard.md`, incluindo ciclos reais com input. O smoke atual não substitui essa aceitação visual. |
| 17559: módulos originais | Parcial | Carregamento seletivo AOT, recursos, XUI e memória implementados. Não executa o XAM completo como firmware. Serviços ainda geram stubs/erros. |
| 6717: build | Implementado e testado agora | Alvo compilado. Não implica funcionamento da dashboard. |
| 6717: funcionamento | Bloqueado parcialmente por conteúdo e instabilidade | Falta `XenonJKLatin.xtt` correspondente; smoke atual terminou em acesso nulo. Tentativa posterior com debugger foi inconclusiva. `preparation.toml` já a restringe a diagnóstico, sem sessão guiada/importação. |
| Outras versões Blades/NXE/Kinect/Metro/beta | Não demonstrado | Arquivos locais identificados não constituem suporte. Não foram gerados/recompilados todos os conteúdos do inventário. |
| Seleção de versões no aplicativo | Parcialmente implementado/testado | Catálogo e separação dashboard/jogo existem; não há troca universal validada entre versões com sessões equivalentes. |
| Enumeração em My Games e seleção original | Implementado com validação histórica | Conteúdo agregado e resolução de identidade testados agora; seleção real documentada para 17559/Castle. Não foi repetido o ciclo interativo completo nesta auditoria. |
| XSYSLAUNCH e dados de lançamento | Implementado; contratos testados agora | Handoff comum e testes do supervisor. Histórico registra 1020 bytes preservados em ida e retorno de Castle. |
| Dashboard → jogo → dashboard | Implementado para combinação específica; histórico | `docs/dashboard.md`, runs `20261012T134846Z`, `134932Z`, `135022Z`. Não declarar todos os jogos/dashboards compatíveis. |
| Retorno versus fechar janela versus crash | Implementado e testado em contratos | Motivos de saída distintos; crash/timeout não são tratados como retorno normal. O caminho XAM de retorno demonstrado não comprova a interface inteira do Guide. |
| Iruka | Regressão automatizada aprovada agora | L1–L5, quadros distintos, 322 cores quantizadas, diferença 6,675, zero stubs/erros/críticas após correção apenas do ambiente do teste. |
| Castle Crashers | Build e smoke aprovados no escopo limitado | 60 s, zero stubs/erros/críticas. Jogabilidade e input atuais não foram confirmados visualmente; evidência funcional anterior em `docs/validation-castle-crashers.md`. |
| Perfis locais reais e sign-in/sign-out | Parcial / fluxo original não concluído | SDK mantém perfil único com XUID fixo e estado conectado. UI original de perfil ainda não é visível/operável. |
| Saves separados por usuário | Não demonstrado | Diretórios de dados por título existem. Não há prova de dois usuários reais com saves isolados e reabertura persistente. |
| Conquistas | Não demonstrado como serviço completo | Não há aceitação ponta a ponta de desbloqueio, persistência e consulta por perfil real. |
| Guide, seleção de usuário e save/load originais | Parcial / não concluído | Pedidos HLE e bindings existem; faltam host contínuo de UI, composição, input e lifecycle completos. |
| Avatares: metadata, TOC e lifecycle C++ | Implementado e testado agora | Testes de catálogo, limites de solicitações, lifecycle e código PPC passaram; probe real sem falhas. |
| Avatares: AOT seletivo de XAM 17559 | Implementado e testado agora | Carregamento sem entrypoint, gerador original, callbacks, montagem completa de assets e preservação do contexto demonstrados. |
| Avatares: `Initialize` em Motocross | Implementado e testado agora, opt-in | Com `--motocross_avatar_service=true`, retornou `00000000`; assets e mipmaps também. Sem habilitar o serviço, o probe isolado passa e a chamada posterior do jogo retorna falha — são modos diferentes. |
| Avatares: personagem/animação em Motocross | Parcial | Histórico inclui tutorial com defeitos de terreno/personagem. O teste atual não aprova visual, animação ou gameplay. `GetManifestLocalUser` ainda retorna `80070490`. |
| Avatares: Keflings | Não implementado/validado no mesmo nível | Protocolo 2 e integração do consumidor não demonstrados; nenhum alvo preparado foi executado agora. Não inferir suporte a partir de Motocross/protocolo 4. |
| Jogos XNA | Não implementado | Intenção no checklist; nenhum runtime gerenciado, loader de assemblies e fluxo XNA completos foram demonstrados. |
| GUI: primeira execução, biblioteca e importação | Implementado e testado no ZIP antigo disponível | Janela real, biblioteca vazia e importação de XEX 17559 observadas. Identificou corretamente versão e conteúdo incompleto. |
| GUI: preparação AOT sem terminal | Implementado, parcialmente validado | Botões acionam scripts reais e processos supervisionados; testes do modelo passam. Preparação completa no ZIP extraído não foi repetida com instalação de ferramentas. |
| Atualização, migração e rollback | Não implementado de forma suficiente | Reabertura pode aceitar workspace antigo apenas porque existe `deps.lock.json`. |
| Windows x64 | Única plataforma exercitada | Máquina de desenvolvimento, inclusive ZIP em diretório independente. Não é Windows limpo. |
| Linux/macOS/ARM64 | Capacidade parcial no SDK; produto não validado | Supervisor, sessão, preparação e UI ainda dependem de Windows; não houve build nem execução dessas plataformas. |

## C. Problemas encontrados

Não foi demonstrado problema de severidade crítica. “Alto” abaixo significa risco concreto para integridade, confiabilidade ou publicação, não alegação de execução remota de código.

### C1. Alto — parser de ISO acessa memória fora do arquivo antes da validação

**Estado: corrigido.** Patch SDK `0037-gdfx-bounded-directory-parser.patch`, sem mudar a revisão fixada. Validação de spans antes de construir ponteiros, travessia iterativa, detecção de registros repetidos/sobrepostos, limites de profundidade/quantidade e publicação da árvore somente após validação completa. Evidência original preservada abaixo; ver E6 para os resultados posteriores.

- **Local:** `src/content/main.cpp:251`, `ExtractDisc`; SDK `src/filesystem/devices/disc_image_device.cpp:90–105`, `Verify`, e `:133`, `ReadEntry`.
- **Evidência:** `DiscImageDevice::Initialize()` percorre diretórios antes de `Extractor::Walk`. O parser usa setor/offset da raiz e registros sem verificar todos os limites do mapeamento. Um ISO sintético local de 35 setores, com raiz no setor 2097152, provocou `STATUS_ACCESS_VIOLATION`; pilha capturada aponta leitura em `ReadEntry:137`. Run `20261013T041355Z-audit-bad-gdfx-root`.
- **Impacto:** arquivo malformado derruba a ferramenta de importação. Limites do extrator posterior não protegem essa fase. Não foi demonstrada execução de código nem escape de diretório por esse caso.
- **Correção:** aritmética verificada para setores, tamanhos e offsets; validar o registro inteiro antes de ler nome/filhos; detectar ciclos e limitar profundidade/quantidade de entradas. Aplicar no parser, com patch reproduzível e testes negativos, antes de conectá-lo ao importador gráfico.
- **Ação imediata:** concluída neste incremento. Integração da extração à GUI continua fora deste trabalho.

### C2. Alto — “pronto” não vincula XEX, código gerado, executável e DLLs

**Estado: corrigido no incremento C2.** `scripts/build-title.ps1` publica um recibo SHA-256 por título/build, verificado por `catalog::Inspect`, pelos perfis de sessão e por `x360n run` antes de criar o processo. Recibo ausente, incompleto ou divergente exige preparar novamente; a existência do executável não basta. Ver E7 para escopo e validação.

- **Local:** `src/desktop/catalog.cpp:166`, `Inspect`; `src/supervisor/paths.cpp:92`, `CheckLaunchable`; `src/supervisor/session_profile.cpp:29`; SDK `src/codegen/output_stamp.cpp:67`, `ComputeInputFingerprint`.
- **Evidência:** a inspeção verifica SHA dos inputs, existência/localização do `.exe` e existência das DLLs. Não compara seus hashes com um recibo do build esperado. `catalog_test.cpp` aceita executável sintético para testar esse estado. O fingerprint do codegen agrega inputs, flags e string de versão do SDK; não inclui digest do gerador, templates ou conjunto de patches. Builds locais reportam versão `dev.unknown` e módulos já atualizados.
- **Impacto:** mudar um patch como 0031/0036, analysis ou toolchain pode deixar código gerado/binário antigo reutilizável. Os hashes registrados pelo supervisor ajudam investigação posterior, mas não impedem execução incompatível. Não atribuo o crash da 6717 a essa hipótese sem prova.
- **Correção:** recibo atômico por preparação contendo identidade de inputs/analysis, commit e patches do SDK, gerador/templates, toolchain/opções, executável e DLLs. Compará-lo em `Inspect` e a cada lançamento; invalidar codegen quando seu produtor mudar. Distinguir “conteúdo reconhecido” de “preparação válida”.
- **Ação imediata:** concluída para C2; isolamento das saídas concorrentes de build (C5) permanece para o próximo incremento.

### C3. Médio — extração sobrescreve o manifesto lateral existente

**Estado: corrigido.** `src/content/extraction_manifest.*` cria staging exclusivo, verifica escrita/flush/close e publica sem substituir o destino. Arquivos, diretórios e links preexistentes são preservados; erro de extração não publica manifesto. Evidência original preservada abaixo; ver E6.

- **Local:** `src/content/main.cpp:214`, `ExtractTree`, especialmente abertura de `dest.filename() + ".sha256"`; `DestinationFree` verifica apenas a pasta destino.
- **Evidência:** uma extração sintética válida sobre um destino novo substituiu o arquivo sentinela preexistente `out.sha256` e retornou sucesso. `std::ofstream` abre com truncamento; abertura/escrita do manifesto também não é validada.
- **Impacto:** perda de arquivo adjacente e sucesso aparente sem manifesto íntegro. Não foi necessário usar symlink para reproduzir.
- **Correção:** reservar pasta e manifesto com semântica de não sobrescrita, rejeitar links/reparse points relevantes, verificar erros de escrita e publicar o manifesto completo de forma atômica.
- **Ação imediata:** concluída junto de C1.

### C4. Alto — nova versão do aplicativo pode reutilizar ambiente antigo silenciosamente

- **Local:** `src/desktop/main_win32.cpp:607` e `:766`; validação de payload em `src/desktop/workspace.cpp`, `InitializeEnvironment`.
- **Evidência:** escolha/reabertura do ambiente pula `InitializeEnvironment` quando existe `deps.lock.json`. A validação de correspondência do payload dessa função não protege esse caminho. O ZIP disponível é anterior à integração final de avatares.
- **Impacto:** a janela de uma versão nova pode preparar/executar fontes e ferramentas antigas; a biblioteca pode continuar marcando binários antigos como prontos. A atualização não tem contrato de migração/rollback.
- **Correção:** comparar identidade do payload instalado ao abrir/selecionar o ambiente; apresentar atualização explícita, preservar inputs/saves, invalidar somente preparações afetadas e permitir recuperação se interrompida.
- **Ação imediata:** antes da primeira Release pública; coordenar com C2.

### C5. Médio — staging concorrente de DLLs e saídas globais do SDK

- **Local:** `cmake/x360n_sdk_backports.cmake:83–112`, `x360n_stage_runtime_dlls`; SDK `CMakeLists.txt:226–228`.
- **Evidência:** vários targets fazem `copy_if_different` das mesmas DLLs para `tests/Debug`. O build paralelo padrão falhou copiando `rexruntimerd.dll`; o build serial no host concluiu. A topologia é compatível com corrida de cópias, embora o log isolado não exclua interferência externa. Builds distintos de títulos escrevem também em `${REXGLUE_ROOT}/out/${REX_PLATFORM}`.
- **Impacto:** falhas intermitentes e possibilidade de uma configuração sobrescrever artefatos utilizados por outra. O lease da GUI/scripts não serializa invocações CMake diretas.
- **Correção:** um produtor de staging por destino e saídas do SDK vinculadas ao diretório/configuração do build. Associar as DLLs ao recibo C2. Não tratar `--parallel 1` como solução permanente.
- **Ação imediata:** antes de automatizar builds simultâneos/CI e publicar preparação para usuários.

### C6. Médio — flags semânticas do código PPC não são uniformes

- **Local:** SDK `CMakeLists.txt:133–134`; configuração de títulos em `titles/*/generated/rexglue.cmake` e CMake dos títulos.
- **Evidência:** o SDK aplica `-fno-strict-aliasing` e `-ffp-model=strict`; os comandos Ninja inspecionados para código gerado de Iruka não contêm ambas. As opções de um subdiretório SDK não passam automaticamente ao target criado pelo pai.
- **Impacto:** os testes PPC podem executar sob premissas de otimização diferentes das dos jogos. É risco de correção futura; não foi isolado um erro numérico causado por isso nesta auditoria.
- **Correção:** explicitar o contrato de compilação dos targets PPC em um único helper/interface CMake realmente consumido por eles; verificar comandos efetivos e casos numéricos sensíveis.
- **Ação imediata:** antes de atribuir equivalência semântica entre a suíte PPC e todos os builds de títulos.

### C7. Alto — usuário fixo e UI de sistema incompleta impedem fidelidade dos fluxos

- **Local:** SDK `include/rex/system/xam/user_profile.h:221`, `signin_state`; `src/system/xam/user_profile.cpp:28`; `KernelState::user_profile_`; bindings de UI em `titles/dash_17559/src`.
- **Evidência:** estado conectado constante, XUID `0xB13EBABEBABEBABE` e um único perfil no kernel. Criação/layout de GamerCardScene já existem, mas a implementação ainda desmonta a cena sem host contínuo de composição/input, conforme código e `docs/dashboard.md`, seção final de UI.
- **Impacto:** perfis reais, troca de usuário, isolamento de saves e avatar do usuário não estão concluídos. Não se resolve apenas desenhando uma caixa no launcher.
- **Correção:** uma fatia ponta a ponta: UI original, criar/selecionar dois usuários, eventos de sign-in/sign-out e save/load persistente separado. Conectar identidade, armazenamento e metadata de avatar ao mesmo usuário.
- **Ação imediata:** antes de anunciar console funcional; pode permanecer limitação explícita em uma prévia técnica restrita.

### C8. Médio — compartilhamento de avatar ainda incorpora ABI/endereço de uma versão

- **Local:** `titles/common/avatar/avatar_aot_assets.cpp:14–39`, `Scoped` e constantes `kProtocol`/`kCallbacks`; `avatar_aot_catalog.cpp`; `titles/motocross_madness/src/avatar_service.cpp`.
- **Evidência:** endereços como `0x81B4B238`, `0x81A975CC` e `0x81A975E0` pertencem ao XAM 17559. O serviço serializa acesso e restaura globais, o que é correto, mas executa requisições de forma síncrona e oferece apenas o contrato validado de protocolo 4. Verificação por base/tamanho não substitui identidade criptográfica do módulo.
- **Impacto:** outro XAM/consumidor pode compartilhar layout aparente e ter ABI distinta; Keflings não fica suportado por reutilizar os parsers. Metadata de usuário, animação e APIs customizadas ainda têm lacunas.
- **Correção:** manter o backend explicitamente 17559, vinculado ao hash do módulo; compartilhar parsers/lifecycle agora e extrair binder comum quando o segundo consumidor real demonstrar o contrato. Testar protocolo 2 e callbacks/overlapped antes de declarar compatibilidade.
- **Ação imediata:** antes de habilitar outra versão/consumidor; não migrar o backend experimental inteiro para o SDK agora.

### C9. Médio — suíte completa depende de encoding e ambiente PowerShell

- **Local:** `tests/local/inputs_test.cpp:50,78,92`; `scripts/regress-iruka.ps1:26`; contraste com normalização já existente em `prepare-tools.ps1`.
- **Evidência:** inventário falhou com exceção de conversão Unicode, após 973 asserções, ao encontrar fixture local com caracteres não representáveis na página de códigos. O teste Iruka falhou antes de iniciar o jogo porque `Get-FileHash` não era encontrado pelo Windows PowerShell com `PSModulePath` herdado. Repetição focada com caminho de módulos correto passou.
- **Impacto:** sinal de regressão confuso e falhas dependentes do nome de usuário/pasta/shell. Apagar a fixture Unicode esconderia um caso válido.
- **Correção:** usar representação UTF-8 explícita nos diagnósticos/caminhos de teste e inicializar o ambiente PowerShell na fronteira do harness. Preservar teste com Unicode e espaços.
- **Ação imediata:** antes de usar a suíte como gate confiável. São falhas observadas agora, sem prova de que foram introduzidas pelo último merge.

### C10. Alto — publicação sem licença própria clara e inventário suficiente de terceiros

- **Local:** raiz do repositório; `scripts/package-desktop.ps1:49–53`; `deps.lock.json`.
- **Evidência:** não há `LICENSE`/`NOTICE` próprio versionado. O pacote copia licenças de fmt, CLI11 e toml++, mas seu payload exporta também patches/código e aponta o SDK por URL; não há inventário completo de avisos ReXGlue/Xenia e demais componentes aplicáveis ao material distribuído.
- **Impacto:** usuários e redistribuidores não têm termos claros para o código do projeto; não é possível aprovar a completude dos avisos apenas pela lista atual. Esta é uma constatação do conteúdo, não parecer jurídico de compatibilidade de licenças.
- **Correção:** escolher licença do código próprio, inventariar o que efetivamente entra em cada pacote e preservar os avisos exigidos de terceiros, inclusive código derivado. Manter conteúdo proprietário e código AOT derivado fora da distribuição.
- **Ação imediata:** antes de qualquer Release pública.

### C11. Médio — empacotamento não estabelece sozinho binário/fonte correspondente

- **Local:** `scripts/package-desktop.ps1:16–20,43–48`; ausência de `.github/workflows`.
- **Evidência:** o script usa cache/preset geral, compila e só depois exige `X360_DESKTOP_ONLY=ON`. Na main usada nesta auditoria o cache é de build completo. O bloqueio de árvore suja cobre apenas parte das entradas; `git archive HEAD` exporta todo o fonte enquanto CMake/headers fora dessa lista podem influenciar o binário local. Não há pipeline versionado de release/CI.
- **Impacto:** processo depende de preparação manual ou da worktree histórica e não garante, sozinho, que o pacote corresponda integralmente ao commit declarado. O ZIP anterior não valida a main consolidada.
- **Correção:** diretório próprio de build de distribuição, configuração explícita desde o início, fonte imutável/entradas verificadas e gate de pacote. Executar em Windows limpo sem ferramentas prévias e registrar o caminho de instalação/recuperação das ferramentas.
- **Ação imediata:** antes da primeira Release. Não é necessário introduzir uma infraestrutura grande de CI para corrigir o contrato local.

### C12. Médio — limite diagnóstico encerra sessões normais longas

- **Local:** `src/supervisor/main.cpp:639–685`, loop de sessão limitado a 32 transições.
- **Evidência:** o limite também vale no fluxo normal da GUI, independentemente de timeout ilimitado. Ao atingir o teto, termina com `session: transition limit reached`.
- **Impacto:** depois de aproximadamente 16 ciclos jogo/retorno, conforme etapas consumidas, uma sessão saudável termina como falha. É consequência estática do código, não teste de endurance executado agora.
- **Correção:** tornar o teto opção diagnóstica ou detectar repetição sem progresso preservando sessões normais. Testar múltiplos ciclos com filhos sintéticos.
- **Ação imediata:** antes de uso contínuo público.

### C13. Médio — 6717 falhou na execução atual; causa ainda não isolada

- **Local:** `titles/dash_6717`, recursos correspondentes e runtime; logs identificados em E.
- **Evidência:** build passou, mas a primeira execução terminou em `0xC0000005`, leitura guest `0x00000000` na thread `0xF8000028`, após 24,167 s. Houve 26.803 tentativas de abrir a fonte ausente. Tentativa com `--crash-stack` não capturou exceção e terminou por timeout; portanto a falha não foi reproduzida de forma determinística.
- **Impacto:** a evidência histórica de renderização/navegação não permite afirmar que este build é estável. Não há base para culpar o merge de avatares, uma instrução PPC ou a fonte ausente sem isolamento.
- **Correção:** reproduzir serialmente com conjunto de recursos e hashes congelados, capturar pilha e diferenciar ausência de recurso de defeito do runtime. Manter 6717 fora do fluxo guiado até nova aceitação.
- **Ação imediata:** para qualquer anúncio de suporte a 6717; não bloqueia sozinho uma prévia restrita à 17559.

### C14. Baixo — documentação de estado contradiz o código integrado

- **Local:** `docs/architecture.md`, `docs/compatibility.md`, trechos de `STATUS.md`, `CLAUDE.md`, `README.md` e `titles/README.md`.
- **Evidência:** coexistem descrições de títulos ainda vazios, Motocross não integrado, ausência do ciclo dashboard/jogo e conjuntos antigos de 10/20/26 patches, enquanto a main possui cinco alvos, integração e 36 patches. README ainda descreve extração de disco como não integrada, embora o comando exista.
- **Impacto:** planos podem repetir trabalho concluído ou promover um resultado histórico a estado atual sem revisão.
- **Correção:** separar estado vigente de diário histórico, atualizar apenas afirmações de status e manter runs antigos como evidência datada. Este relatório não alterou os outros documentos.
- **Ação imediata:** antes da publicação; não requer reorganização do repositório.

## D. Riscos arquiteturais e de produto

### D1. Crescimento do catálogo e fidelidade

O principal limite não é a quantidade de diretórios em `titles/`. É a combinação conteúdo × versão de SDK/patches × codegen × toolchain × backend gráfico. Um recibo pequeno e verificável resolve um problema real; um sistema abstrato de plugins sem segundo consumidor não resolve esse problema.

Continuar com manifests por hash, overrides locais, módulos seletivos e processo por título. Não injetar endereços 17559 em APIs genéricas nem fazer fallback automático para recursos de outra dashboard. O experimento de região/memória de 17559 e seus bindings XUI devem continuar opt-in/locais até existir contrato demonstrado em outra versão.

XNA é uma frente arquitetural adicional, não outro XEX a acrescentar ao manifest. Exige decisão baseada em conteúdo real sobre runtime gerenciado, assemblies, APIs de framework e integração com o mesmo modelo de sessão/perfil. Não precisa ser implementado antes de estabilizar o fluxo nativo.

### D2. SDK, patches e reprodutibilidade

ReXGlue fixado em `v0.10.0`, commit `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`; LLVM fixado em `22.1.8`. `deps.lock.json` contém hashes dos patches e dos arquivos resultantes. CMake rejeita conteúdo divergente e o aplicador respeita a ordem. Esta disciplina deve ser preservada.

Foram verificados os **36 patches**, cobrindo **45 caminhos distintos**: 44 no SDK e um no submódulo SDL. As modificações rastreadas do SDK correspondem ao conjunto declarado. A diferença do libmspack vem da materialização de 15 links; cada arquivo materializado foi comparado com o alvo e coincidiu. Os commits dos submódulos recursivos foram conferidos. Não encontrei alteração rastreada inexplicada fora desse conjunto.

A aplicação/reversão foi ensaiada em fixture isolada, sem reverter o SDK em uso. A fixture contém os arquivos afetados da revisão fixada, inclusive a base do SDL e `.gitattributes`. Após reversão, 45/45 hashes coincidiram com os blobs originais; reaplicação dos 36 patches passou. Resultado: `revert=0 checked=45 different=0; reapply=0`. Uma primeira fixture sem `.gitattributes` expôs somente diferença CRLF/LF; foi corrigida na fixture antes da comparação final. Não é falha dos patches reais.

Os patches que tocam o mesmo arquivo são dependentes de ordem: por exemplo 0013–0016 em conteúdo; 0018/0020 no SDL; 0009/0019/0026–0034 em XAM; 0029/0031 em módulos/dispatch. Não tratar cada patch como aplicável isoladamente a qualquer revisão.

Classificação principal de todos os patches, sem afirmar que já seriam aceitos upstream:

| Classe | Patches | Juízo técnico |
|---|---|---|
| 1 — correções gerais potencialmente upstream | 0001–0004, 0007–0008, 0013, 0015, 0018, 0020–0021, 0023, 0025, 0029–0030, 0033, 0035–0036 | Memória/teardown/imports, DPC/input, conteúdo, foco, sincronização/áudio, identidade/diagnóstico, recursos e semântica PPC. Evitam ou corrigem defeitos gerais, mesmo quando descobertos num título. |
| 2 — extensões necessárias à plataforma | 0006, 0010–0012, 0014, 0016–0017, 0019, 0022, 0026–0028, 0031–0032, 0034 | Serviços XAM, conteúdo/language/preferences, callbacks de lançamento/retorno/UI, superfície embutida, dispatch host-only e alocação. Precisam preservar contratos e falhar explicitamente fora das classes suportadas. |
| 3 — compatibilidade específica de dashboard | 0009 | Resolve ABIs antigos/novos de data/hora por heurística de argumentos. É ajuste de compatibilidade global que pede testes de fronteira e versões explícitas; não comprova todas as gerações. |
| 4 — aproximação/experimento que requer validação adicional | 0005, 0024 | PTE de imagem ainda aproxima comportamento; apresentação persistida foi demonstrada em escopo 17559/D3D12. Existência de caminho Vulkan não equivale a aceitação em Vulkan/outras GPUs. |

Há limites de validação também nas classes 1/2: 0023 não declara teste dedicado no lock; 0029/0030 citam ensaios live; foco/teardown e apresentação exigem teste real de janelas/GPU. Contagem de patches com testes não mede compatibilidade. Os ensaios atuais de Iruka, Castle e 17559 não demonstram todas as combinações de patches em todos os títulos.

Não recomendo migração de SDK. Recomendo separar candidatos upstream maduros, com teste mínimo que falha antes da correção, das pontes experimentais. O risco atual maior é C2/C5, não o número 36 por si só.

### D3. Experiência do usuário e comparação com Pinyon Shift

Foi consultado o [repositório oficial do Pinyon Shift](https://github.com/arcanite24/pinyon-shift) e inspecionado o fluxo nos arquivos locais de referência já disponíveis: `docs__INSTALLING.md`, `launcher__PinyonShift.Launcher__MainWindow.xaml.cs`, arquivos de `Desktop/Installation` e `PortableMode`, XAML e `release.json` identificado como v0.5.1. O snapshot local não é checkout Git verificável; não atribuo seu conteúdo a um commit remoto específico. Nenhum conteúdo de jogo foi baixado.

| Etapa | Pinyon Shift observado | x360-native observado / consequência |
|---|---|---|
| Primeira execução | Instalação e estado existente orientam o fluxo. | ZIP abre GUI real, com ação principal de configuração. Bom ponto de partida. |
| Importação | Pasta/ISO e identificação do conteúdo esperado. | Arquivo/pasta com identificação por hash e diagnóstico de faltantes. ISO ainda é ferramenta CLI e precisa C1/C3 antes de integração guiada. |
| Preparação | `setup-preview.ps1` acionado pelo launcher, com eventos estruturados. | Scripts reais, logs e estágios; não é botão decorativo. Falta nova aceitação completa do pacote consolidado. |
| Erros e cancelamento | Resume causa/primeiro erro e encerra árvore de processos. | Supervisor, cancelamento e detalhes existem. Mensagens de dependência/compilação ainda precisam teste com leigos e Windows sem toolchain. |
| Reutilização | Confere fonte do payload e metadados do build. | Valida inputs, mas artefatos existentes bastam para prontidão: C2. |
| Atualização/reinício | Diretórios de fonte versionados e retomada após reinício. | Sem contrato equivalente de migração/retomada: C4. |
| Ambiente portátil | Verifica escrita e restrições de caminho. | Usa ambiente local e separa armazenamento; Unicode, permissões e caminhos longos merecem aceitação do fluxo inteiro. |
| Experiência principal | Launcher específico para seu jogo. | Biblioteca administra preparação; jogo continua iniciado pela My Games original. Preservar essa diferença de objetivo. |

Um usuário leigo consegue abrir a GUI e importar um arquivo reconhecido no pacote testado. **Não há evidência suficiente para afirmar que consegue concluir instalação de dependências, preparação, ciclo e recuperação sem ajuda.** A importação real marcou um `dash.xex` isolado como conteúdo incompleto e impediu preparação prematura — comportamento correto. Não instalar ferramentas nem baixar conteúdo durante esta auditoria foi compatível com o escopo solicitado; isso deixa a aceitação da primeira instalação pendente.

A melhor referência a aproveitar é o vínculo entre payload e preparação, a retomada e a mensagem acionável. Não há motivo demonstrado para copiar framework, renderer ou arquitetura do jogo de referência.

### D4. Distribuição e portabilidade

- O frontend Windows usa link estático do runtime do compilador no modo desktop-only. Abrir o ZIP não exige CMake/LLVM; preparar um título ainda exige toolchain, MSVC STL/CRT e Windows SDK, que a GUI/scripts podem provisionar com consentimento. Esta máquina já possui essas dependências.
- O pacote exporta fonte próprio e templates, não o SDK checkout nem conteúdo original. `prepare-tools.ps1` reutiliza caches, confere SHA-256 de arquivos baixados e fixa SDK/submódulos. O instalador Microsoft usa validação Authenticode; não é uma imagem integralmente congelada do toolchain instalado.
- Caminhos absolutos observados em CMake caches, logs e reports são locais/ignorados. Eles não devem virar pré-requisito do pacote. A worktree de distribuição é conveniência histórica, não requisito aceitável do produto: C11.
- O lease de workspace, verificações de raiz e preservação de saves são bons controles. Precisam compor uma atualização transacional; não bastam separadamente.
- `RunProcess` retorna erro explícito fora de Windows; `SessionWindow` exige Windows; `CheckLaunchable` exige `.exe`; UI Win32, `devshell.cmd`, PowerShell e presets amd64 completam a dependência atual. Não existe produto Linux/macOS executável validado nesta revisão.
- O caminho para portabilidade é implementar processos/janela/preparação nas fronteiras existentes e testar toolchains/backends. Não recomendo reescrever agora a UI só por portabilidade futura. Não anunciar ARM64, Linux ou macOS por capacidade do SDK.
- A organização desejada continua sendo um pacote principal por sistema/arquitetura suportados, com dados e preparações separados do aplicativo. Hoje somente o candidato Windows x64 tem evidência operacional.

### D5. Segurança, conteúdo, licenças e histórico

Foram examinados os 249 arquivos rastreados e nomes dos objetos alcançáveis pelas referências Git locais. Não foram encontrados XEX, XEXP, fontes Xbox, ISO/STFS, executáveis/DLLs, PDBs ou arquivos de jogo nas categorias pesquisadas, inclusive arquivos posteriormente removidos ainda alcançáveis. Os únicos caminhos versionados sob `generated/` são cinco templates `rexglue.cmake`, não corpos PPC derivados de conteúdo proprietário. `.local` e saídas AOT ficam ignoradas; o empacotador também rejeita essas categorias. Isso deve permanecer como gate automatizado.

A busca de padrões de alta confiança em 71 commits alcançáveis não encontrou credenciais do tipo AWS access key, GitHub token, `sk-proj` ou chave privada PEM. Não é prova de ausência de todo segredo: não cobre formatos arbitrários, objetos inalcançáveis/reflogs ou históricos remotos não disponíveis. Não foram lidos nem expostos arquivos pessoais de credenciais.

Todos os 71 assuntos de commits alcançáveis seguiram Conventional Commits na verificação efetuada. Não foram encontradas atribuições automáticas a assistentes nas mensagens; referência legítima a `CLAUDE.md` não foi tratada como autoria. Nada foi reescrito e nenhum crédito foi removido.

As importações GUI verificam identidade, raízes e links; o supervisor recusa executar um `.exe` de `.local/inputs`. São controles úteis, mas o parser nativo de ISO ainda viola a fronteira de conteúdo não confiável (C1), e C3 permite perda de arquivo lateral. A auditoria não demonstrou escape de extração para caminho arbitrário; não o deduz de um crash.

Logs/proveniência podem conter caminhos absolutos e nome do usuário do Windows. Não encontrei envio automático desses logs no fluxo inspecionado. Ao oferecer compartilhamento de diagnóstico, deve haver revisão/redação de caminhos e exclusão de conteúdo proprietário. Não versionar os logs desta auditoria.

O impedimento de publicação mais objetivo nesta área é C10: licença própria e avisos de terceiros ainda precisam ser resolvidos. O fato de o SDK declarar BSD não licencia automaticamente todos os arquivos do projeto ou seu payload.

## E. Verificações executadas e limites

### E1. Ambiente e artefatos de evidência

Windows x64 na máquina de desenvolvimento, LLVM 22.1.8 e ferramentas já instaladas. Builds de títulos foram incrementais nos cinco alvos existentes; não houve regeneração forçada de todo o catálogo nem download de conteúdo. Execuções que exigiam criação de processos, assembler ou temporários do host foram realizadas fora do sandbox após a tentativa restrita revelar a limitação ambiental.

Logs de auditoria: `.local/validation/project-audit-20261013/`. Reports de execução: `.local/runs/<identificador>/report.json`, acompanhados de `rex.log`, stdout/stderr e, quando aplicável, frames. Os reports incluem SHA-256 de executável/DLLs; registram o que foi executado, sem provar o vínculo de produção discutido em C2.

### E2. Comandos e resultados

Comandos abaixo são relativos à raiz do repositório. O PowerShell foi usado para capturar logs; a tabela omite apenas redirecionamentos.

| Verificação / comando | Resultado nesta auditoria |
|---|---|
| `git status --short`, `git rev-parse HEAD`, `git log --all --not main`, `git merge-base --is-ancestor <commit> main`, inspeção das worktrees | Integração confirmada no alcance descrito em A; alterações preexistentes preservadas. |
| `powershell -NoProfile -File scripts/apply-sdk-patches.ps1 -Check` | 36 patches/hashes consistentes; log `patch-check.log`. |
| Reversão/aplicação em fixture isolada dos arquivos afetados | 45 arquivos iguais à base após reversão, reaplicação aprovada; `patch-replay/result.txt`. Não representa clone/build completo novo. |
| `scripts/devshell.cmd cmake --build --preset win-amd64-debug` | Falhou no staging concorrente de DLL: `build-main.log`. |
| Mesmo build com `--parallel 1`, primeiro restrito e depois no host | Tentativa restrita falhou ao executar assembler PPC; host concluiu. `build-main-serial.log`, `build-main-host.log`. |
| `scripts/devshell.cmd ctest --preset win-amd64-debug -L project -E local[.] -j 8` | **107/107 passaram**, 36,22 s; `tests-focused.log`. Inclui supervisor, modelo desktop, handoff, SDK checks e avatares. |
| `scripts/devshell.cmd ctest --preset win-amd64-debug -j 16` | **Uma execução completa: 1.784 testes, 1.777 aprovados, 4 pulados, 3 falhas**, 141,82 s; `tests-full.log`. |
| `ctest --preset win-amd64-debug -R local.iruka.regression --output-on-failure`, via devshell com `PSModulePath` dos módulos Windows PowerShell | Repetição focada aprovada, 25,67 s; `test-iruka-retry.log`. A suíte completa não foi repetida. |
| `cmake --build .local/build/titles/<alvo> --target <alvo> --parallel 6`, via devshell, um alvo por vez | Iruka, Castle, 17559, 6717 e Motocross compilaram; cinco logs `build-<alvo>.log`. |
| `x360n-content extract-disc <ISO sintético> <destino novo>` | ISO válido permitiu demonstrar sobrescrita do manifesto; ISO malformado causou acesso inválido antes da extração. Fixtures em `extract-probe`. |
| Extração do ZIP existente e conferência de `package.json` | 240 arquivos conferidos por hash; supervisor `--help` e GUI executados no diretório independente. |
| GUI real do ZIP | Primeira execução, biblioteca vazia, importação de `dash.xex` 17559 e estado “Conteúdo incompleto” observados; hash da cópia igual ao original. |
| `git rev-list --objects --all`, inspeção dos nomes, mensagens e busca de padrões de segredo em commits | Resultado e limites em D5; logs locais `history-objects.txt`, `commit-subjects.tsv`, `commit-messages.txt`. |

As três falhas da suíte completa foram:

1. `unit.Depfile escapes characters make treats as syntax`: falha já conhecida/documentada do SDK no Windows. Continua aberta, não foi escondida por filtro na suíte completa.
2. `local.inventory of .local/inputs classifies every XEX`: exceção Unicode, C9. Nova observação; causalidade com a integração não estabelecida.
3. `local.iruka.regression`: `Get-FileHash` indisponível no ambiente herdado, antes da execução de Iruka. Passou na repetição focada com ambiente corrigido, sem alterar código.

Os quatro skips pertencem a testes `unit.BitStream` e não foram contados como aprovados. A distribuição dos 1.784 testes é 1.458 PPC, 213 unit e 113 project; alto número de testes PPC não mede quantidade de jogos compatíveis. `sdkcheck.Vavguw` exercita 66.560 verificações de médias unsigned, overflow e aliasing de destino; isso é evidência específica para 0036, não para toda a renderização dos avatares.

### E3. Execuções com conteúdo local

Forma dos smokes: `x360n run <exe> --label <label> --timeout <segundos> --rex-log -- --gpu_plugin=xenos --mnk_mode=true --game_data_root=<conteúdo local> --user_data_root=<dados isolados da auditoria>`. Supervisor: `.local/build/win-amd64/src/supervisor/Debug/x360n.exe`; executáveis em `.local/build/titles/<alvo>/`. Flags adicionais abaixo delimitam os modos testados.

| Run | Resultado e interpretação |
|---|---|
| `20261013T041513Z-regress-iruka` | L1–L5 passaram; 322 cores, diferença entre frames 6,675; zero stubs/erros/críticas. Timeout diagnóstico de 20 s, não saída natural do título. |
| `20261013T041724Z-audit-dash17559-intro` | Flag adicional `--dash17559_boot_intro=true`; 45,137 s, encerrado pelo supervisor. Sequência original da intro concluiu e entrou na dashboard. 10.435 linhas de stub, 1.950 mensagens unimplemented, 1.987 erros, zero críticas. Não aprovação de todos os serviços ou da experiência visual/sonora. |
| `20261013T042122Z-audit-castle` | 60,198 s, timeout; zero stubs/erros/críticas. Não foi obtida captura confiável do jogo em primeiro plano para nova aceitação de menu/gameplay/input. |
| `20261013T042235Z-audit-dash6717` | Crash `0xC0000005` após 24,167 s; 37 stubs, 2 mensagens unimplemented, 9 erros, zero críticas; leitura guest nula. Fonte ausente registrada. |
| `20261013T042755Z-audit-dash6717-stack` | Tentativa focada com `--crash-stack --timeout 35`: nenhuma exceção capturada, término após 70,010 s; 40.836 falhas de abertura da fonte. Inconclusiva, com perturbação do debugger e sobreposição temporal com o teste Motocross; não apaga o crash anterior nem é teste de estabilidade. |
| `20261013T042317Z-audit-avatar-service-probe` | Flags `--motocross_xam_aot_probe=true --motocross_avatar_service_probe=true`, roots de XAM e catálogo 17559. Gerador, quatro metadatas, montagem completa e restauração da ABI/contexto: zero falhas no probe. Sem habilitar serviço normal, a chamada posterior `Initialize` do jogo retorna `80004005`, esperado nesse modo. Timeout 30,232 s. |
| `20261013T042808Z-audit-avatar-service` | `--motocross_avatar_service=true`, mesmos roots. `READY`, `Initialize=00000000`, assets `decoded=13 applied=7 finalized=1 guards=true`, `GetAssets` e mipmaps bem-sucedidos. Manifesto do usuário ainda ausente; 4 stubs, 5 erros, zero críticas. Timeout 30,382 s. Sem nova aceitação visual. |
| `20261013T041355Z-audit-bad-gdfx-root` | Fixture sintética, falha de leitura capturada no parser GDFX: C1. |

Para Motocross, os roots adicionais são `--x360n_xam17559_root=.local/inputs/dashboard/versions/17559/retail_system_assets/extracted` e `--x360n_avatar_catalog_17559=.local/inputs/dashboard/versions/17559/retail_system_assets/storage_packs/FFFE07DF00000002.extracted/AvatarAssetPack.toc`, passados como caminhos absolutos nas execuções. Não houve aquisição de novos assets.

Uma contagem zero de “critical” não significa ausência de crash: 6717 é o contraexemplo concreto. Os contadores classificam mensagens, não garantem semântica; os próprios reports alertam que são registros de processo.

### E4. Pacote Windows testado

Pacote preexistente: `x360-native-0.1.0-preview.18-windows-x64.zip`, encontrado na worktree de distribuição, 1.740.090 bytes. SHA-256: `8f1ed08b71b89c97a26b4238b81f0a7d62b9463d359c78b012379def85519d75`. Commit declarado: `9ecc07315ddbdb4f5596aba2917bcc52a661ed74`.

Extraído em `%LOCALAPPDATA%/Temp/x360-project-audit-20261013/x360-native-0.1.0-preview.18-windows-x64`, fora da árvore do repositório. Conferidos os 240 arquivos do inventário. `x360n --help` funcionou. A GUI abriu, exibiu biblioteca vazia e importou uma cópia do XEX 17559; reconheceu versão e faltantes, sem promover a preparação a pronta.

**Limites:** pacote anterior ao HEAD auditado; PATH e dependências da máquina de desenvolvimento disponíveis; não houve teste em Windows limpo, nova instalação MSVC, download, preparação integral nem atualização de uma versão instalada. Os relatos históricos de builds portáteis e ciclo via preview não foram contabilizados como novas execuções.

### E5. Não executado / não aprovado por inferência

- Aceitação interativa atual completa da 17559, ciclo My Games/Castle/retorno, perfil, Guide ou save/load.
- Gameplay/áudio prolongado de Castle e Motocross; validação visual da animação/terreno dos avatares.
- Keflings, XNA, catálogo inteiro, todas as versões de dashboard e todos os recursos disponíveis no inventário.
- Vulkan/outras GPUs, Linux, macOS, ARM64 e instalação em Windows limpo.
- Update/rollback do aplicativo e retomada após reinício de instalação de ferramentas.
- Fuzzing exaustivo, auditoria jurídica de licenças e busca de objetos Git inalcançáveis.

### E6. Correção posterior de C1/C3 — 13/10/2026

Implementação e testes registrados no commit local `530919d` (`fix(content): bound GDFX parsing and publish manifests without replacement`).

Escopo: parser GDFX no SDK, extração/manifesto em `src/content`, testes focados e documentação. Sem mudança de revisão do SDK, de conteúdo proprietário, do catálogo, launcher ou trabalho funcional dos títulos.

**C1:** `DiscImageDevice` verifica os 20 bytes do magic, os campos do descritor, spans completos da raiz/diretórios/arquivos e cada registro de 14 bytes mais nome. Cálculo de setor usa intermediário de 64 bits e comparação por subtração antes de formar ponteiros. A travessia mantém ordem esquerda/entrada/direita sem recursão entre irmãos; registros repetidos ou sobrepostos rejeitam ciclos e aliases de tabelas. Diretórios têm o limite de 32 MiB já usado para a raiz, profundidade máxima 256 e orçamento de 1.048.576 entradas. Uma falha deixa a árvore pública nula. Arquivos fora da imagem agora rejeitam o filesystem inteiro na inicialização, em vez de extrair parcialmente os arquivos anteriores.

**C3:** pasta destino reservada por criação exclusiva; manifesto escrito em `<dest>.sha256.partial` com `std::ios::noreplace`. Uma extração sem erros e stream escrito/flushado/fechado com sucesso é publicada por `MoveFileExW` sem replacement no Windows, seguindo o precedente do repositório. O ramo POSIX usa hard link sem substituição. Colisão tardia também falha sem alterar o arquivo existente. Staging preexistente é preservado; staging próprio é removido ao abandonar a extração. Falha pode deixar arquivos extraídos para inspeção, mas não um manifesto final que sugira sucesso. `out/` e `out/.` são normalizados para o mesmo destino e manifesto adjacente de `out`.

| Verificação | Antes | Depois |
|---|---|---|
| Sete casos iniciais `ctest ... -R extract-disc --output-on-failure -j 1` | 4 falharam, 3 passaram; reproduzidos `0xC0000005`, `0xC00000FD`, sobrescrita e manifesto em extração com erro | Cobertura ampliada para 12 casos de extração/publicação, todos aprovados na suíte final |
| 23 variantes sintéticas malformadas | Incluíam crash/estouro de pilha ou aceitação indevida | Todas rejeitadas com código 1, sem timeout, pasta de saída ou manifesto |
| Ciclos de irmãos e de diretórios, profundidade excessiva | Recursão não limitada | Erro controlado; cadeia válida de 300 irmãos continua aceita |
| Manifesto sentinela, destino ocupado, staging arquivo/diretório/hard link | Manifesto final podia ser truncado | Conteúdo existente preservado; abertura/publicação recusadas |
| Colisão criada depois de abrir staging, stream em falha, abandono | Sem contrato transacional | Sem substituição nem publicação de manifesto parcial; testes determinísticos do publicador |
| ISO válida, diretórios aninhados/vazios, arquivos vazios, partição deslocada e ordem da árvore | Controles válidos aprovados | Continuam aprovados, com conteúdo e SHA-256 esperados |
| Fixtures originais `.local/validation/project-audit-20261013/extract-probe/{bad-root,disc}.iso` | Crash da raiz inválida documentado na auditoria | Reutilizadas sem alteração: malformada retorna 1; válida retorna 0 e extrai `hello.txt` com bytes `hello` e SHA-256 `2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824` |
| `scripts/devshell.cmd cmake --build --preset win-amd64-debug --target x360n_tests --parallel 1` | Build dos testes de reprodução aprovado | Build final aprovado, incluindo SDK/parser e ferramenta, sem recompilar títulos |
| `scripts/devshell.cmd ctest --preset win-amd64-debug -R x360n[.] --output-on-failure -j 4` | — | **66/66 aprovados, zero falhas/skips**, 16,63 s; conteúdo, supervisor e smoke PPC existentes |
| `scripts/apply-sdk-patches.ps1 -Check` e verificação CMake | 36 patches existentes | 37 patches e hashes consistentes; pin `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2` preservado |
| Aplicação/reversão de 0037 em fixture Git isolada com `.gitattributes` | — | Hashes dos dois arquivos iguais ao resultado declarado após aplicar e à revisão original após reverter |

A revisão independente encontrou um caso de compatibilidade com separador final no destino; foi reproduzido, corrigido e incluído nos testes antes da conclusão. A primeira fixture de replay sem repositório Git não respeitou os atributos LF e divergiu por finais de linha; a comparação final usou fixture Git com os atributos do SDK. Não foi revertido o SDK em uso.

Limites: verificação executada em Windows x64; ramo POSIX inspecionado, sem build/execução Linux. Falha de escrita foi exercitada por estado de erro do stream, não por encher um disco. Não houve nova suíte completa, execução de jogos/dashboards, fuzzing exaustivo ou nova validação de STFS proprietário. O publicador e a normalização de destino são comuns aos comandos `extract` e `extract-disc`; o parser STFS não foi alterado. As falhas gerais da auditoria fora de C1/C3 permanecem abertas.

## E7. Correção de C2 — identidade da preparação

- Recibo `<título>.exe.preparation.json`, adjacente ao executável, vincula título e versão pelos XEX exatos, análise transitiva/manifesto (depfile do SDK), registro existente, revisão/patches do SDK, fontes/headers da dependência real do Ninja, templates, compilador/ferramentas/configuração CMake, código gerado e EXE/DLLs finais. O inventário `CONFIGURE_DEPENDS` também detecta adição/remoção de fontes e templates. Não foi criado outro catálogo.
- Publicação por arquivo temporário, flush e rename/replace no mesmo diretório; falhas mantêm o marcador `.incomplete`. Entradas e produtor são conferidos antes/depois do build; DLLs publicadas devem corresponder aos bytes e ao inventário das saídas do grafo. O gerador executado é uma cópia verificável no build do título, preservando sua identidade quando outro build substitui o staging compartilhado do SDK. C5 não foi resolvido nem a arquitetura do supervisor alterada.
- Preparação válida é reutilizada diretamente, sem relink de intermediários não certificados. Preparação inválida reconstrói os produtos nativos somente do projeto solicitado; o codegen usa hashes por módulo existentes e `--ignore-stamp` quando muda o produtor, sem `--force` nem supressão de erros. Módulos gerados antigos fora do grafo não entram no recibo. Alterar análise de outro título não invalida este; alterar dependências compartilhadas invalida seus consumidores, que só são preparados quando solicitados.
- A identificação permanece SHA-256, com o mesmo contrato de `DigestFile`; Windows usa BCrypt para tornar viável verificar ferramentas e dependências por conteúdo, inclusive em Debug, sem cache baseado em mtime. O supervisor continua sem linkar o runtime ReXGlue. O pin e os 37 patches de C1/C3 foram preservados.

| Evidência | Resultado |
|---|---|
| Antes/depois em `.local/audit/c2/baseline` | Supervisor anterior iniciou o executável substituto e registrou saída 42 do filho. Supervisor corrigido retornou 3 por falta de recibo e não criou outro run. |
| Troca deliberada de EXE/DLL em fixture isolada | Recibo correto permite executar o filho; substituir o EXE por outro PE ou os bytes da DLL retorna 3, sem criar diretório de run/processo de título. |
| XEX, análise, manifesto, patch, template, compilador, codegen, configuração e artefatos | Mudanças de bytes, inclusive com mesmo tamanho/mtime, são rejeitadas; revisão diferente, DLL adicional/ausente, recibo parcial, título errado e marcador incompleto também. Análise de um título não invalida o outro; patch compartilhado invalida ambos. |
| Testes focados | 68 verificações em 9 casos SHA-256/recibo; 104 verificações em 22 casos do catálogo; publicação PowerShell testa criação, substituição, falha preservando recibo anterior, inventário de templates, invalidação de headers gerados e recusa de DLL divergente/fora do grafo. |
| Regressões do projeto | `ctest -C Debug -L project -E "local[.]|env[.]" --output-on-failure -j 4`: 121/121 aprovados, incluindo C1/C3 e contratos de handoff/retorno, em 49,90 s. |
| Castle Crashers | Build completo com 2.871 entradas e quatro artefatos no recibo; run `20261013T061343Z-c2-castle`, timeout diagnóstico de 60 s (62,282 s medidos), zero stubs/erros/críticas. Repetição com artefatos finais `20261013T063826Z-c2-final-castle`: fechamento normal aos 59,604 s, código 0, também zero stubs/erros/críticas. |
| Iruka | Run final ocioso `20261013T065116Z-regress-iruka`: L1–L5 aprovados, 330 cores, diferença entre quadros 5,219, zero stubs/erros/críticas; timeout diagnóstico de 20 s. Uma repetição durante compilação pesada capturou o primeiro quadro preto (L4 falhou); critérios e script de regressão não foram alterados. |
| Dashboard 17559 | Run final `20261013T065337Z-c2-final-dash17559`: intro original retornou 0 e iniciou a dashboard; timeout diagnóstico de 45 s (45,181 s medidos), sem críticas/crash. Persistem 10.473 stubs, 1.934 mensagens unimplemented e 1.971 erros dos serviços incompletos, comparáveis à auditoria original. Não é aceitação desses serviços nem repetição do ciclo interativo My Games/retorno. |
| Catálogo real após builds sequenciais | Teste `[.preparation-local]`: 8/8 verificações aprovadas; Castle Crashers, Iruka e 17559 reconhecidos como preparados, mesmo após a reconstrução do SDK pelo título seguinte. |
| Reutilização real final | Segundo preparo de cada título retornou `reused verified preparation`, sem executar build nem mudar o hash do recibo ou qualquer EXE/DLL. Castle: 2.871 entradas/4 artefatos; Iruka: 2.903/4; 17559: 3.282/7. Resultados em `.local/audit/c2/reuse-results.json`. |

Logs e recibos são locais em `.local/audit/c2` e `.local/build/titles`; conteúdo proprietário não foi alterado ou versionado. A auditoria original subestimou o fingerprint do SDK: ele já inclui o hash dos templates embutidos; faltava vincular o produtor real e o conjunto completo da preparação aos artefatos aceitos pelo aplicativo.

O teste de reutilização real encontrou uma comparação literal entre o caminho do compilador escrito pelo CMake com `/` e o inventário Windows com `\`. A normalização foi corrigida e coberta no teste PowerShell antes da conclusão. As tentativas em que os scripts mudaram durante a compilação recusaram a publicação e mantiveram o marcador incompleto, como esperado.

## F. Próximas prioridades

| Ordem | Categoria | Entrega de maior impacto | Critério de conclusão |
|---|---|---|---|
| 1 | **Concluído: C1/C3** | Fronteira de conteúdo corrigida; ver E6 | ISO truncado, offsets extremos, ciclos e manifesto preexistente retornam erro controlado; nenhum crash/sobrescrita nos testes; extração válida preservada. |
| 2 | **C2 concluído; C5/C6 pendentes** | Isolamento de build e premissas PPC — C5/C6 | Identidade/invalidação implementadas em E7; ainda faltam builds paralelos independentes e mesmas premissas PPC nos targets. |
| 3 | Antes da primeira Release | Pacote reproduzível, atualização segura e gate de publicação — C4/C9/C10/C11/C12/C14 | Licença/avisos presentes; fonte/binário vinculados; falhas da suíte resolvidas ou explicitamente bloqueantes; pacote do HEAD testado em Windows limpo, com importação/preparação/cancelamento/retomada e preservação dos dados numa atualização. |
| 4 | Evoluir progressivamente; antes de anunciar console funcional | Completar uma fatia original de perfis, UI de sistema e armazenamento — C7 | Dois usuários, sign-in/out, save/load isolado, reabertura e retorno pelo fluxo original, com testes de ciclo de vida e input real. |
| 5 | Evoluir progressivamente | Compatibilidade por evidência: estabilizar 6717 e segundo consumidor de avatar — C8/C13 | 6717 com recursos correspondentes e causa de crash isolada; Motocross com visual/animação aceitos; Keflings/protocolo 2 testado antes de generalizar backend. Depois ampliar versões e plataformas. |

Na auditoria original, as próximas **três tarefas de desenvolvimento** eram: (1) extrator seguro, (2) preparação/artefatos confiáveis, (3) candidato Windows publicável e atualizável. C1/C3 e a identidade de preparação C2 foram concluídos; C5/C6 e o candidato publicável permanecem pendentes.

**Não mexer agora:** migração de SDK, troca de linguagem/framework, fusão de todos os jogos no processo da GUI, universalização do backend de avatar ou implementação de XNA/portabilidade em paralelo à estabilização desses contratos. Também não reescrever histórico nem remover créditos.

## G. Decisões a preservar

1. **Dashboard original como experiência de console.** O aplicativo prepara, diagnostica e inicia a sessão; My Games permanece responsável pelo lançamento percebido pelo usuário.
2. **Processo por título, supervisor fora do runtime.** Isola falhas e permite separar retorno, fechamento e crash. Corrigir o teto de transições sem abandonar esse limite.
3. **Identidade por hash e assets por versão.** Rejeitar conteúdo desconhecido/incompleto e evitar mistura automática de firmware. Estender essa identidade aos artefatos compilados.
4. **SDK fixado e patches reversíveis com hashes/testes.** A reprodução dos 36 patches foi bem-sucedida. Preservar a ordem e tornar exceções explícitas.
5. **Handoff comum com flags e dados originais.** Evita inventar um protocolo diferente em cada jogo; manter validação nas duas pontas.
6. **Dispatch AOT seletivo e tabelas host-only.** Resolve colisões reais de imagens guest sem exigir boot de todo o XAM; manter escopo e identidade dos módulos.
7. **Parsers/lifecycle de avatar em C++ compartilhado, backend original delimitado.** O probe e `Initialize` real demonstram valor. Generalizar apenas contratos comprovados por outro consumidor.
8. **Modelo desktop independente da UI Win32.** Permite testes rápidos de importação/catálogo e futura implementação de UI em outras plataformas, sem antecipar abstrações.
9. **Conteúdo proprietário, AOT e logs fora do Git/pacote.** Manter essa fronteira na CI e na distribuição.
10. **Falha explícita para funcionalidade ausente.** Não transformar stubs de perfil, avatar, XNA ou Guide em sucesso fictício para melhorar a aparência dos logs.

O projeto deve continuar nessa base, com prioridade à confiabilidade da preparação e aos fluxos originais completos. A expansão segura do catálogo depende mais desses contratos verificáveis do que de acrescentar novos títulos que apenas compilam.
