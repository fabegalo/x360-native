# STATUS

Correção da configuração AOT genérica em caminhos longos (2026-10-14):
o Forza importado falhou no try_compile do SDL porque o build usava o ID completo
como diretório. O catálogo e os dados mantêm a identidade completa; o build usa
uma chave curta com proprietário verificado, e o codegen vai para esse build.
O driver segue o binaryDir declarado no preset, preserva fontes/stamps e inclui
as novas dependências no recibo. Roteamento segue o native_exe do manifesto;
executáveis sem registro no novo diretório continuam recusados. Testes focados
de geração/identidade/sessão (6 casos/99 asserções), recibos e regravação do glue
aprovados; geração real passou 11 asserções. Na instalação afetada, configure
passou em 406 s e o produtor em 148 s. Forza parou no codegen (86 s): 12 chamadas
sem função correspondente, em 11 destinos únicos; não compilado nem jogável.
O CLI do SDK regrava seu glue; a integração própria agora usa arquivo separado.
Hotfix.4 final passou nos gates do pacote, foi aplicada na mesma instalação e
preservou os quatro hashes de catálogo/perfis/saves/preferências. Regeneração
final passou 11 asserções; configure com glue do SDK regravado também passou
(4,8 s de configure/generate), mantendo o diagnóstico real do codegen. Backups
dos pacotes/fontes anteriores verificados. Sem iniciar jogo, Computer Use ou
publicação. Evidências e limites: `docs/universal-library.md`.

Automação de Releases no GitHub Actions (2026-10-14): workflow manual e por
tag `v*` implementado em `.github/workflows/release.yml`, reutilizando
`scripts/package-desktop.ps1`. O runner Windows prepara ferramentas/SDK
fixados, aplica/verifica os 67 patches, compila Release e executa os gates
focados de distribuição, licenças/conteúdo e ZIP/procedência. Execução manual
`38515926098` aprovada no commit `3615c7aa277f367187ce0b00d32a078615c95b43`:
17 testes de tooling, `desktop.catalog`, 88 componentes/3 executáveis e upload
passaram. ZIP Windows x64 e SHA-256 baixados e reconferidos, 491 arquivos,
3.516.821 bytes; digest `b7c6d196aace73b5ca6be3462403942f67f6328423c3d877149ad762503039f0`.
Preparação corrigida do LLVM: 67 s após substituir a decodificação XZ lenta
do TAR do Windows pelo 7-Zip do runner, mantendo arquivo/hash/staging.
Comparação de caminho no teste AOT corrigida para identidade física diante
do alias Windows `RUNNER~1`, preservando o contrato do entrypoint. Workflow
rápido de PRs e isolamento de versões preservados. Publicação por tag exige
notas correspondentes, verifica assets em draft e trata previews como
pre-releases; somente simulada nos testes, sem criar tag ou publicar Release.
Sem Computer Use, sessão de jogo ou suíte completa. Evidências e operação:
`docs/release-automation.md` e `.local/validation/ci-release-success.log`.

Correção da importação do Forza Horizon na instalação preview.20 (2026-10-14):
a ISO extraía 2.432 arquivos sem erro, mas o caminho temporário de cópia excedia
MAX_PATH no diretório longo do pacote. A mensagem localizada do filesystem
também quebrava a serialização UTF-8 do catálogo e ocultava a causa. A cópia
agora usa caminhos estendidos do Windows para staging/publicação; erros de
arquivo persistem com código e caminhos UTF-8, e o JSON é serializado antes
de abrir o temporário. Reprodução sintética falhou antes da mudança; seis
testes focados passaram (31 asserções). A retomada real na instalação informada
pelo usuário passou (8 asserções), reaproveitou arquivos e conferiu o inventário
completo de 7.320.461.527 bytes. Pacote local preview.20-hotfix.2 aprovado pelos
gates de distribuição e aplicado na mesma pasta após autorização do usuário;
432 fontes conferidas, catálogo/perfis/saves/preferências com hashes preservados.
Backup local verificado do pacote anterior. Sem executar jogo, Computer Use ou
regressão completa. Evidências e limites: `docs/universal-library.md`.

Correção da preparação limpa da preview.20 (2026-10-14): a primeira configuração
exigia AOT pré-existente de signin/createprofile/vk/hud. O driver agora configura
o produtor, gera explicitamente todos os manifests da própria dashboard e só
então exige seus módulos na reconfiguração final. Identidades originais e os 67
patches preservados. Instalação extraída afetada, com espaços no caminho e sem
fontes geradas: configure 9 s, produtor 119 s, codegen principal 45 s, módulos
4/3/3/3 s, reconfigure 9 s, build 198 s; executável, DLLs e recibo produzidos.
Logs locais: `.local/logs/preview20-installed-prepare.log` e
`.local/logs/preview20-cold-receipt-tests.log`. Substituição da Release quebrada
autorizada pelo usuário; pacote corrigido mantém a versão preview.20 e recebe
novo commit/tag e SHA-256. Sem nova sessão de jogo ou Computer Use.

Biblioteca universal + AOT automático (2026-10-14), **candidata local preview.20**:
catálogo persistente por inventário SHA-256, importação ISO/GDFX/STFS/pasta,
metadados verificados, versões/duplicatas e retomada sem destruir dados.
Projetos manuais são perfis opcionais; o desktop gera projeto AOT genérico,
executa codegen/build com o SDK pinado e registra somente builds verificados
na My Games original. Falhas de preparação/runtime permanecem diagnosticáveis.
Charlie Murder LIVE, sem projeto manual, foi importado, gerado, compilado e
iniciado pela dashboard original; bloqueio real em `0x82163C60` ausente da
tabela AOT, **não jogável**. Reuso de build e persistência após erro comprovados.
Castle Crashers LIVE e Forza Horizon ISO importados; três entradas persistem
após reinício. Castle recompilado e iniciado pela My Games até o menu, sessão
limitada sem erros/stubs/unimplemented; não amplia o aceite de saves comerciais.
Testes focados, screenshots reais e logs em `docs/universal-library.md`.
C7/C8, isolamento de versões e todos os 67 patches preservados. Sem Computer
Use, push ou publicação; ZIP Windows x64 preparado pelo gate de distribuição.
Importação universal e tentativa automática **não significam execução universal**.

C8 (2026-10-13), **saves XContent por perfil/título implementados e validados
pelo caminho XAM → NT/VFS → disco em Windows x64**: patch 0067 usa o XUID
conectado do C7 e Title ID do XEX, isola enumeração/mounts, invalida handles
antigos ao trocar de perfil e efetua flush real. Dois processos independentes
gravam/recuperam payloads diferentes para Player1/Player2; exclusão de um não
atinge o outro. Treze casos focados distintos cobrem armazenamento, C7 e
conteúdo instalado; replay dos 67 patches com hashes conferidos. Layout e dados
anteriores preservados. Castle Crashers recompilado e iniciado por scripts,
mas **save/load comercial ainda não comprovado**: as sessões chegaram ao menu
sem produzir save. Sem Computer Use ou regressão completa. Contratos, reprodução,
evidências e limites em `docs/save-storage.md`; o aceite amplo do ciclo de jogo
permanece pendente.

C7.8 (2026-10-13), **isolamento obrigatório dos originais por versão**:
`AGENTS.md`/`CLAUDE.md` registram a regra permanente. O vk.xex efetivamente
integrado é Retail **2.0.17559.0**, SHA-256 `876ddbb333d83f408ab7c69267703e623daef6da5b1eb3302bd94d246155724e`.
Build/codegen e executável direto agora recusam dependências ausentes ou com
hash divergente antes de usar originais; runtime verifica uma vez por início,
sem cache global de validação, e mantém caches/metadados dentro da versão.
A 6717 recusa a fonte ainda não identificada, sem herdar fonte da 17559.
Quatro testes focados distintos aprovados, incluindo arquivos reais e versão
interna/imports do teclado. Criação com caracteres escolhidos no teclado 17559
e GamerCards de `asPlayer1`/`qwPlayer1` demonstradas por scripts locais.
Mapeamento, limites e evidências em `docs/dashboard.md` §0.32. SDK e patches
0001–0066 preservados, sem Computer Use, jogos, push ou Release.

C7.7 (2026-10-13), **múltiplos perfis locais funcionais pela UI original da
Retail 17559/Windows x64**: partindo do HEAD local `c132d12`, dois perfis foram
criados do zero por Guide → Sign-in → Create Profile → teclado original → Done.
Player1 e Player2 aparecem na seleção, entram alternadamente, têm a identidade
correta na GamerCard e saem pelo Sign-out original. Reinícios recuperam os dois
XUIDs e o estado conectado/desconectado; uma execução adicional demonstrou
Player1 → Sign-out → Player2 no mesmo processo. Nenhuma fixture criou os perfis.
O backend mantém uma conta ativa no slot 0, migra o registro C7 de um perfil
para a coleção v2 e publica mudanças somente após gravar. Patches 0054–0066,
SDK pinado inalterado; 17 casos focados distintos aprovados ao longo dos
incrementos e replay dos 66 patches com hashes base/finais conferidos.
Builds incrementais e sessões por scripts locais de até 60 s, com PNGs, logs,
hashes dos binários e snapshots. Sem Computer Use, jogos, push ou Release.
Evidências, contratos e reprodução: `docs/dashboard.md` §0.31. Xbox Live,
Guide completo, edição de avatar, saves/conquistas e vários jogadores
simultâneos continuam fora deste aceite. Os resultados anteriores são históricos.

C7.6 (2026-10-13), **Sign-in local existente funcional na Retail 17559/Windows x64**:
partindo de `15a0251`, os patches 0044–0053 corrigem o modo retail, contratos
de LogonEx, tarefas XAM, configuração persistente e notificações. `80070525`
era consequência de um stub de `XamIsDataCenterMode` que ativava o ramo errado;
a tela vazia após login era mantida pelo descarte de `XNotifyBroadcast`.
Execuções locais com entrada e capturas reais demonstram o perfil existente
`C73Local`, A realizando login, retorno automático à Home, GamerCard com o nome,
B fechando/cancelando e recuperação da mesma identidade após reiniciar.
14 testes focados distintos passam; 53 patches reaplicados sobre fontes do SDK
pinado com hashes conferidos. Sem Computer Use, jogos, push ou Release.
O alcance é um perfil local existente, slot 0; criação pela interface, múltiplos
perfis e Xbox Live continuam fora desta comprovação. Evidências e reprodução:
`docs/dashboard.md` §0.30. Os bloqueios C7.3–C7.5 abaixo são históricos.

C7.5 (2026-10-13), **parcial; login original ainda bloqueado**: patch 0043
corrige `RtlCompareMemoryUlong`, que retornava palavras iguais em vez de bytes
do prefixo igual. A dashboard compara os quatro bytes zerados de passcode com
retorno 4; receber 1 criava um falso cadeado. Metadata/persistência estavam
corretas e foram preservadas. Dois testes focados passam; o teste de comparação
falhou antes do patch. Na única sessão gráfica (56,114 s), o perfil apareceu
sem cadeado e não houve pedido de passcode. O usuário confirmou que A levou a
uma tela vazia e B não voltou. Arquivo idêntico, sessão zero; dashboard conectada
não demonstrada. O host `signin.xex` ainda falha com `80070525` (NO_SUCH_USER),
antes da confirmação; a origem dessa falha não foi determinada. Limite gráfico
consumido, nenhum processo remanescente. Evidências e próximo ponto exato de
rastreamento em `docs/dashboard.md` §0.29; nenhum aceite funcional promovido.

C7.4 (2026-10-13), **bloqueado; login original não entregue**: uma única
execução manual de 58,153 s, a partir de `3d6045c`, mostrou `C73Local` e o
modal “Enter Pass Code”. O usuário confirmou que A não entrou e B não fechou.
A sessão isolada permaneceu desconectada, com arquivo idêntico antes/depois.
O pedido real `XamShowMessageBoxUI` (`0x00020003`, zero botões) entra no
modal genérico do SDK, sem tratamento desse modo de entrada/cancelamento;
a conclusão overlapped depende de fechar esse modal. A rota separada do host
`signin.xex` também falhou na criação com `80070525`. Dependências e limites
em `docs/dashboard.md` §0.28. Investigação encerrada sem ampliar subsistemas,
sem patch funcional, sem repetir backend/builds ou executar outra sessão.

C7.3 (2026-10-13), **parcial; confirmação pela cena ainda não demonstrada**:
patch 0042 conecta os imports de enumeração/consulta de conta do Sign-in original
ao perfil persistido do 0041, preservando XUID de 64 bits. Uma execução dirigida
com timeout mostrou `C73Local`, vindo de uma fixture isolada em `local-v1.dat`,
na interface original. A sessão permaneceu desconectada; nenhum botão foi
acionado por automação. O contrato LogonEx `0x120420`, usado pela confirmação
normal da 17559, altera a sessão real e é recuperado em outro runtime nos testes.
Isso **não comprova seleção/confirmação pela UI nem atualização visual após login**.
Cinco testes de perfil passam; SDK/persistência/host existentes reutilizados.
Correções pequenas de XMemCpy/XMemSet/lstrcpynW, encontrados como stubs na execução,
receberam testes focados; não houve segunda execução gráfica. Detalhes em
`docs/dashboard.md` §0.27. Criação pela cena, outros modos de logon e tarefas
XAM completas continuam pendentes. Nenhum aceite completo de C7 foi marcado.

C7.2 (2026-10-13), **parcial; Sign-in funcional ainda não entregue**: patch
0041 substitui a identidade fixa por um registro local persistente de um perfil,
com criação no serviço C++, entrada/saída pelos exports XAM Logon/LogonEx no modo
local flags=0, consultas de identidade e notificações após mudança gravada.
Sem registro, o runtime inicia desconectado. Persistência em
`<user_data_root>/profiles/local-v1.dat`; erro de gravação não publica sessão.
**Bloqueio da interface:** enumeração, dados de conta e tarefas de criação do
Sign-in original continuam ausentes. Não há caminho de criação/seleção utilizável
pelo usuário neste incremento; não criar um perfil fixo para esconder essa lacuna.
Detalhes e validação focada em `docs/dashboard.md` §0.26.
Três testes `sdkcheck.Local profile` passam (0,95 s, timeout 30 s por caso),
cobrindo XAM, reinicialização, overlapped e falhas de persistência.
Build de `dash_17559` e DLL Sign-in concluído, sem execução; 41 patches verificados.
O usuário confirmou que GamerCard e Sign-in originais aparecem, recebem entrada
e fecham; essa evidência substitui a pendência visual abaixo, sem revalidação.

C7.1 (2026-10-13): integração de `signin.xex` 17559 ao host XUI existente,
com geração AOT separada (mesma base de gamerprofile), pedido FE/21001 de
20 bytes e usuário FF preservados. Patch 0040 cobre sessão ANY_USER e entrada
HUD Ex; 3 testes focados / 76 asserções passam. **Criação, apresentação,
navegação e fechamento da cena original ainda não validados em execução.**
Build do alvo `dash_17559`, incluindo `dash_17559_signin.dll`, concluído com saída 0.
Não houve Computer Use, sessão gráfica ou regressão de jogos. Serviço de perfis
continua incompleto; ordinal XAM 054B tem trap explícito. Procedimento manual
e limites em `docs/dashboard.md` §0.25. C7/GamerCard conserva sua evidência anterior.

Primeira pre-release (2026-10-13): **`v0.1.0-preview.19`, Windows x64,
First Technical Preview**, **publicada e verificada sem autenticação** em
13/10/2026. Tag e pacote usam `ed60e5a1fa94d116a9e472e70dc366d665985b90`;
repositório público, ZIP e SHA-256 acessíveis e conferidos por download anônimo.
Implementações técnicas de **C1/C3, C2, C5, C6, C9, C12, C4/C11 e
C10 concluídas**. Windows limpo continua **não validado**, como limitação desta
prévia experimental. [Release e identidade dos artefatos](docs/FIRST_RELEASE.md).

Incremento de extração (2026-10-13): **C1 e C3 corrigidos**. Patch reproduzível
0037 limita o parser GDFX e rejeita spans inválidos/ciclos antes de expor a árvore;
o manifesto `.sha256` é publicado completo, sem substituir arquivo existente.
66/66 testes relevantes passaram, incluindo 12 casos de extração/publicação;
fixtures sintéticas da auditoria reutilizadas. SDK permanece na mesma revisão,
agora com 37 patches verificados. Sem alterações funcionais dos títulos.
Resultados antes/depois e limites: [auditoria, E6](docs/PROJECT_AUDIT.md#e6-correção-posterior-de-c1c3--13102026).

Atualizado em 2026-10-13. Marcos: **M0 feito**; **M1 feito para um XEX** (amostra
XDK "Dolphin" da `rexglue/demo-iruka`), com as limitações abaixo.

Frente de avatares integrada à `main` por merge com histórico: Motocross Madness
chegou ao **gameplay do tutorial** em 2026-10-13. Contagem regressiva, movimento,
88 MPH, 748 pontos e objetivos de volta/drift/manobra observados na interface.
O serviço opt-in usa catálogo, malhas, modificadores, callbacks e mipmaps originais;
Initialize só aceita depois de abrir o catálogo e montar um avatar completo.
SDK conserva memória, arquivos e conclusão overlapped. A correção `vavguw`
passou 66 560 verificações. A integração recompilou o SDK e executou 104 testes
pertinentes, todos aprovados, em build próprio; os 36 patches foram reaplicados
em uma cópia limpa do SDK fixado, com hashes conferidos.
Na main integrada, os quatro títulos compilaram; 128 verificações dos probes
originais, Iruka L1–L5, tutorial do Motocross e ciclo intro → 17559 → Castle
Crashers → 17559 passaram no escopo documentado na seção 15 de avatares.
Branch e worktree temporárias removidas pelo Git; conteúdo privado preservado
em `.local/preserved/x360-avatars-20261013/`. A main volta a ser a referência.
**Renderização ainda defeituosa:** chão preto e piloto deformado; corrida completa,
save/load, animações do catálogo e componente especial continuam não validados.
Evidência e limites: [avatares, seção 14](docs/avatar-system.md#14-servico-real-e-entrada-no-tutorial-2026-10-13).

## Funciona (com evidência)

Aplicativo gráfico Windows (§§15–17): tela inicial Configurar/Iniciar Xbox 360,
importação e biblioteca separadas. O catálogo de compatibilidade não aparece como
jogos instalados. ZIP executado fora do repositório com PATH do Windows; importação
real por arquivo e pasta da Retail 17559 confirmou estados incompleto/completo e
originais preservados, sem instalar jogos. 34 testes de identidade, importação e
sessão passam. Preview.13 completou importação, downloads, geração e compilação
do zero fora do repositório; iniciou a intro/menu originais pela interface e
reabriu pronto sem recompilar. Fontes de idioma omitidas foram incluídas no
manifesto 17559; textos conferidos na execução, sem distribuir as fontes.
Windows limpo e instalação de MSVC ausente permanecem pendentes. Sem alterações
no runtime ou na frente de avatares neste incremento.
Preview.18 estendeu o teste à biblioteca: importou os 708 arquivos originais de
Castle Crashers 0.0.1.3 e compilou no ambiente externo. Após corrigir caminhos
longos, prontidão parcial e correspondência da cópia instalada ao perfil, o ciclo
My Games → jogo → retorno original à 17559 foi executado e confirmado pelo usuário.
Trocar abas deixou de recalcular hashes; conferências ocorrem no worker e antes
das operações. 35 testes passam. STFS/ISO pelo assistente continuam pendentes;
esta validação usou pasta extraída, sem ampliar a compatibilidade do catálogo.
[Preparação gráfica](docs/desktop-preparation.md). Publicação privada e
reconciliação do histórico anteriores permanecem registradas no checklist §19.3.
Incremento atual: Retail 17559 com fonte original, entrega da URI My Games e
intro original AOT com animação/som confirmados pelo usuário. Export 2(false)
permite que a animação termine pelas próprias verificações e retorne ao menu;
run `20261012T045539Z-dash-bootanim-graceful`. 23/23 sdkcheck e iruka L1–L5 passam.
Castle Crashers executou 45 s sem erro/crash; duas chamadas a AchievementsUI
continuam stub. My Games agora exibe Castle Crashers 0.0.1.3 (confirmação do usuário);
o ciclo dashboard → Castle Crashers → dashboard foi executado e confirmado pelo
usuário (§0.17). O patch 0016 e o supervisor seguem o pedido original, com hash
do XEX e launch data preservados. Janela principal persistente e entrada confirmadas após corrigir foco SDL; Guide e perfis pendentes.
Retorno original agora confirmado: Castle Crashers chamou XamLoaderLaunchTitle(NULL,
0x100); a 17559 recebeu o contexto e voltou sem intro, na mesma janela (§0.19).
31/31 sdkcheck e 40/40 x360n passaram. Iruka rodou 30 s sem erros/stubs, encerrado
por timeout (sem nova captura visual). A continuidade visual entre títulos ainda
tem corte relatado pelo usuário; o ciclo funcional não aprova essa fidelidade.
Incremento §0.21: preservação da imagem e sequência de gamma da 17559 atendidas
pelo perfil; ciclo repetiu sem intro na volta. Áudio pendente é drenado antes
de fechar o stream. Usuário confirmou entrada recuperada e ausência de cortes.
35/35 sdkcheck e 40/40 supervisor passam. Resultado restrito ao par testado;
sem equivalência audiovisual medida contra console.
Incremento §0.24 (C7): GamerCardScene.xur original da 17559 aparece sobre a
dashboard, atualiza, navega e fecha por B. Três ciclos de abertura/fechamento
na mesma execução devolveram o foco à dashboard sem crash. O host usa o mesmo
dispositivo, quadro e apresentação; executa somente rotinas XUI selecionadas,
sem iniciar outro XAM/kernel. Patch 0039 mantém sessão, entrada exclusiva e
pedido de fechamento diferido no runtime. **Sign-in, criação/persistência e
troca de usuários continuam pendentes**; a identidade fixa do SDK não mudou.
Tarefa C7 encerrada em 2026-10-13 por solicitação do usuário, no escopo da
GamerCard: três ciclos e 107 asserções já aprovados, sem nova execução no
encerramento. `4cc7303` pertence ao repositório principal e já inclui o patch
reproduzível 0039 e seu registro no lock; pin do SDK `f5337cdc` preservado.
Checklist oficial de cobertura: `docs/CHECKLIST_FUNCIONAL_x360-native.md`.
Detalhes, regressões e limites: `docs/dashboard.md` §§0.19–0.24. Recursos e código XUI
da 17559 não são compartilhados silenciosamente com outras versões.
Segundo perfil: Retail 6717 apresenta quadros reais (blade "Xbox 360"), troca para
a blade Mídia e move o destaque com entrada roteada; **nenhum texto aparece**:
falta `XenonJKLatin.xtt` da 6717 (aberto por `media:`, ausente do pacote).
XAM 0xAE6 registrado, 334 inícios por ponteiro, `XUsbcamCreate` comum e `\SystemRoot`
montado; `docs/dashboard.md` §0.15.

| Item | Evidência |
|---|---|
| ReXGlue v0.10.0 (`f5337cdc`) compila no Windows com LLVM 22.1.8 + MSVC 14.44, modo submódulo, com 20 patches locais | builds dos alvos afetados com saída 0; patches em `patches/rexglue-sdk/`, verificados no configure e em todo build |
| Suíte PPC do SDK (166 arquivos `.s`) | 1458/1458 casos |
| Testes unitários do SDK | 208 aprovados, 4 `SKIP` upstream, 1 falha (bug do teste, abaixo) |
| Patches locais do SDK: 0001 tabela de funções no heap XEX certo; 0002 falha de setup sem desmontagem prematura; 0003 import com slot e thunk em registros diferentes; 0004 encerramento com a janela ainda viva; 0005 bit 0 de `KeGetImagePageTableEntry`; 0006 idioma e fallback UTF-16; 0007 despacho real de DPC | `sdkcheck.*`: testes antes/depois em `patches/rexglue-sdk/README.md`; o 0004 e o caminho do 0002 testados num `ReXApp` real (`tests/shutdown_probe`); 0006 tem 108 asserções em 2 casos |
| Programa PowerPC próprio pelo `recompile-tests` | `x360n.ppc.*` 4/4 |
| Supervisor `x360n doctor / identify / run` | testes `x360n.*`, `env.doctor`; `identify` classifica XEX2/XEX1/outra variante/inválido; `run` grava hashes do exe e das DLLs ao lado dele, diagnóstico do log e, com `--crash-stack`, a pilha de uma exceção não tratada |
| **Primeiro XEX real pelo fluxo completo** (identify → init → codegen → build → run) | `docs/validation-iruka.md`: `default.xex` `66ec34f2…` lido pelo carregador, 5222 funções geradas de novo pela v0.10.0 (determinístico), compilado, renderiza e anima por 30–40 s sem crash, 0 chamadas a stubs no log |
| Entradas locais | Após organizar e deduplicar as dashboards, `local.inventory …` classifica 484 XEX de `.local/inputs` (472 XEX2 lidos, 4 XEX1, 7 outras variantes, 1 inválido — listados em `.local/runs/inventory/inventory.tsv`); inventário passou com 956 asserções e `local.integration targets …` passou com 35, conferindo os 4 alvos de `tests/local/integration_targets.tsv` |
| **Segundo alvo: Castle Crashers (XBLA)** | `docs/validation-castle-crashers.md`: pacote LIVE extraído com `x360n-content` (708 arquivos), XEX criptografado + LZX lido pelo SDK, 3 bloqueios diagnosticados por desmontagem e corrigidos com ajustes de análise, menu principal renderizado; teclado e controle funcionando **segundo teste manual do usuário**; mesmo resultado com o runtime corrigido (run `cc-run-patched`) |
| Regressão da referência | `local.iruka.regression` (L1–L5) passa dentro da suíte completa, com o runtime corrigido |
| **Dashboard 17559: menu Metro com textos e entrada parcial** | `docs/dashboard.md` §0.12: quadro real preservado em `0195954`; textos aparecem pelo fallback fornecido pelo usuário. Controle e Shift+setas confirmados; menu do sistema abre. My Games chega ao roteador URI ausente; algumas páginas do sistema ainda falham. Região experimental inalterada |
| Reprodução do zero | árvore nova: configure 416 s, build Debug 166 s, testes 29 s (antes dos patches; não refeito) |

Incremento 0008–0010: fila MNK, ABI de data/hora e strings indisponíveis;
integração de fonte do título e falha explícita de URI não implementada.
**22/22 `sdkcheck`**; iruka L1–L5 passou (`20261012T040641Z-regress-iruka`).
Castle Crashers apresentou o menu principal e executou 45 s sem crash, stubs,
erros ou críticas (`20261012T040834Z-cc-regression-navigation`). Os dois testes
terminaram por timeout; não demonstram encerramento normal ou ciclo pelo menu.
A suíte inteira não foi repetida.

Totais da última execução completa (`ctest --preset win-amd64-debug -j 16`,
com os patches 0001–0006, 2026-10-12): **1726 testes, 1721 aprovados, 4 ignorados (upstream), 1 falha** —
`unit.Depfile escapes characters make treats as syntax`, a falha conhecida (item 1).
`local.iruka.regression` passou (L1–L5, run `20261012T022946Z-regress-iruka`).
Castle Crashers: menu observado, 90 s sem crash, 0 stubs/erros/críticas
(run `20261012T023116Z-cc-regression-0006`). Log completo:
`.local/logs/codex-final-ctest-host.log`. A primeira execução restrita foi
interrompida após falhas de ambiente (módulos PowerShell/acesso a executáveis e
temporários); a repetição completa usou os módulos do Windows PowerShell e
acesso ao host, sem alterar testes.

## Comandos executados (resumo)

```text
scripts\devshell.cmd cmake --preset win-amd64                       -> exit 0
scripts\devshell.cmd cmake --build --preset win-amd64-debug         -> exit 0
scripts\devshell.cmd ctest --preset win-amd64-debug -j 16           -> 1721/1726 (4 skip, 1 falha conhecida)
x360n identify .local\inputs\titles\iruka\extracted\default.xex      -> exit 0, XEX2 sem execution info
x360n run rexglued.exe -- init --project-name iruka ...              -> exit 0 (titles/iruka)
x360n run rexglued.exe -- codegen titles\iruka\iruka_manifest.toml   -> exit 0, 77 arquivos
scripts\devshell.cmd cmake -S titles/iruka -B .local/build/titles/iruka -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DREXSDK_DIR=... -> exit 0
scripts\devshell.cmd cmake --build .local/build/titles/iruka         -> 1ª falha (imgui.h), depois exit 0
x360n run .local\build\titles\iruka\iruka.exe --rex-log -- --gpu_plugin=xenos --game_data_root=... -> TIMEOUT (esperado), sem crash
```

Detalhes, mensagens exatas e classificação de cada falha: `docs/validation-iruka.md`.

## Falhas e desvios conhecidos

1. `unit.Depfile escapes characters make treats as syntax` (SDK) falha por escape
   inválido no próprio teste; `sdkcheck.*` comprova que a implementação está certa.
   Continua falhando e é contada como falha, não como sucesso.
2. `rexglue --version` informa `0.10.0.0-dev.unknown` em modo submódulo.
3. Submódulo do SDK aparece modificado: 15 symlinks do libmspack materializados
   como cópias verificadas (a conta não cria symlinks) e os 36 patches locais
   (arquivos listados em `patches/rexglue-sdk/README.md`), reaplicáveis por
   `scripts/apply-sdk-patches.ps1` e registrados com hashes em `deps.lock.json`.
4. Backport do upstream `b5e0cf8` (include de xxHash **e imgui**) em
   `cmake/x360n_sdk_backports.cmake`, usado pela árvore principal e pelos títulos;
   sem ele `unit_tests` e o `rex_app.cpp` de qualquer título não compilam.
5. `install` do SDK não executado (grava em `HKCU\Software\Kitware\CMake\Packages\rexglue`).
6. Cada título em modo `REXSDK_DIR` compila o SDK de novo na própria árvore
   (~7 min de configure + ~3 min de build a frio). As três árvores escrevem a mesma
   DLL em `external/rexglue-sdk/out/win-amd64`; cada título copia a sua no build
   (`x360n_stage_runtime_dlls`), e o `x360n run` registra o hash da que foi usada.
7. A demo `demo-iruka` **como publicada** não compila com a v0.10.0 (foi escrita
   para v0.1.0): formato de configuração, `find_package` de SDK instalado e API C++
   mudaram. É incompatibilidade de integração, não do XEX.
8. A coleção de dashboards fornecida pelo usuário está organizada em
   `.local/inputs/dashboard/versions/<versão>/<variante>/`, com fontes em
   `resources/fonts/` e correspondência dos nomes antigos em
   `metadata/organization/catalog.csv`. A 17559 ativa mantém `dashboard/17559/`.
   Os XEX de pré-lançamento em variantes `blades_beta` continuam não suportados
   (`XEX0`, `XEX?`, `XEX-`, `XEX1`); `versions/8955/nxe_beta/dash.xex` continua
   inválido (cabeçalho XEX2 sem o primeiro byte). Organização e deduplicação não
   corrigem esses formatos; os arquivos únicos foram preservados.
9. Imports `XUsbcam*` sem símbolo no runtime (`src/kernel/CMakeLists.txt:53`) e
   ordinais fora da tabela sem definição gerada: títulos que os usam não linkam
   sem definições próprias (`docs/architecture.md` §7, itens 13–14).

## Bloqueios

| Bloqueio | Impacto | O que destrava |
|---|---|---|
| **Dashboard 17559: experiência de sistema** | ciclo original com Castle Crashers confirmado na mesma janela, mas há corte visual na troca; Guide e perfis incompletos | seguir a apresentação original na transição e o caminho de perfil (Settings chama XamShowGamerCardUI, stub FE/21008) |
| Configurações parciais | algumas páginas leem 0x1C após XConfig categoria 7; strings comuns indisponíveis retornam nulo | corrigir contratos demonstrados; fonte original integrada e XAM 17559 disponível como referência, sem executar seu código |
| Sem symlinks no Windows | exige o passo de materialização | opcional: Developer Mode |

## Não funciona / não existe ainda

- Dashboard: biblioteca e ciclo original com Castle Crashers funcionam na 17559; transição visual, Guide e perfis ainda incompletos. Região de sistema experimental preservada. Castle Crashers sem save/load validado, sem versão completa (licença) e sem teste automatizado de gameplay.
- O `iruka` não foi validado em entrada, áudio, encerramento normal, execução longa nem Release.
- `x360n` não encadeia `init/codegen/build` sozinho: `scripts/build-title.ps1`
  faz o build em quatro fases, mas `init` e o codegen inicial ainda são passos à mão.
- Canal título→supervisor e XamLoaderLaunchTitle(NULL, 0x100) validados; Guide original permanece pendente.

## Próximo incremento de maior valor

1. Dashboard 17559: corrigir a continuidade visual pelo caminho original e seguir
   Guide/perfis locais conforme o checklist oficial. A região zerada permanece
   hipótese explícita no log/relatório; nenhum fade substituto aprova a transição.
2. Castle Crashers: teste de save/load em `.local/user/castle_crashers`
   (gravar no jogo, encerrar, reabrir, conferir), que é a próxima etapa da matriz.
3. Transformar `identify → init → codegen → build-title → run` num comando do
   supervisor com relatório por etapa; os alvos já validados servem de regressão.

## Marcos

- **M0** build reproduzível, SDK integrado, testes reais — feito.
- **M1** um programa PowerPC autorizado em XEX pelo fluxo completo — feito para a amostra Dolphin (limitações acima).
- **M2 (parcial, experimental)** dashboard — versão 17559: renderização contínua, fonte original, navegação, My Games e ciclo original com Castle Crashers; transição visual, Guide e perfis pendentes.
- **M3 (parcial, experimental)** Castle Crashers: inicia pelo menu original, entrada e retorno à 17559 confirmados; save/load e gameplay prolongado não validados.
- **M4–M5** — não iniciados.
