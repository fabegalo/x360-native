# Aplicativo gráfico — candidato Windows x64

**Distribuição de referência:** `v0.1.0-preview.19`, primeira prévia experimental
com publicação pública autorizada. C4/C11 e C10 tecnicamente concluídos; Windows
limpo continua não validado. As seções datadas abaixo preservam os testes dos
candidatos anteriores. Estado ativo: [FIRST_RELEASE.md](FIRST_RELEASE.md).

O pacote tem um aplicativo principal `x360-native.exe`. Supervisor, licenças e
fontes ficam em `app/`; a pasta `data/` criada na primeira abertura guarda o
ambiente local. Preserve `data/` ao remover ou atualizar o aplicativo. Outra
pasta de ambiente ou saves pode ser escolhida nas configurações sem apagar dados.

A tela inicial tem a ação Configurar/Iniciar Xbox 360. Dashboard, Importar e Biblioteca são áreas distintas; a biblioteca só inclui jogos cujo conteúdo foi adicionado, não todo o catálogo de compatibilidade. Estados resumidos e progresso real ficam na tela; listas de arquivos e diagnóstico ficam em Detalhes. É possível adicionar um arquivo ou uma
pasta extraída, inclusive componentes separadamente. O catálogo usa identidades
verificadas; versões desconhecidas passam pelo leitor XEX do supervisor e são
mostradas como identificadas, sem preparação disponível. Formatos antigos são
separados de arquivos corrompidos. Nenhum desses estados significa compatibilidade.

17559 não depende de instalar um jogo. Os jogos opcionais são registrados na
biblioteca original somente com conteúdo completo verificado e execução preparada.
O supervisor lê o catálogo sem exigir todos os jogos instalados e mantém a
validação completa de caminho, hash e executável na troca de título. A interface
não oferece um launcher alternativo de jogos; o lançamento permanece na My Games.
6717 continua identificada com sua pendência própria, sem herdar módulos da 17559.

Referência examinada: [Pinyon Shift](https://github.com/arcanite24/pinyon-shift),
README, `docs/INSTALLING.md`, as duas implementações em
`launcher/PinyonShift.Launcher` e `launcher/PinyonShift.Launcher.Desktop`.
Foram examinados seleção de conteúdo, estado/progresso real, cancelamento,
instalação versionada, persistência e verificação antes de habilitar execução.
A release v0.5.1 foi baixada e seu ZIP conferido contra o digest publicado:
`88841e7741d2750fefea58400a4c28c972379b1080d6c0c3cd726001bd03d5d6`.
Contém `PinyonShiftLauncher.exe` e `pinyon-shift-source.zip`. Licença BSD-3-Clause
conferida; não copiamos código, arte ou dependências do launcher de referência.
Adotamos a separação entre aplicativo e ferramentas internas, preservando nosso
pipeline. A referência verifica a identidade das fontes que produziram o build;
esse vínculo completo ainda não é um aceite do nosso estado de preparação.

`X360_DESKTOP_ONLY=ON` permite compilar apenas frontend, supervisor e testes,
com bibliotecas redistribuíveis e runtime C++ estático. Não recompila o runtime
nem os títulos e não aplica patches no SDK. O pacote recusa recursos proprietários,
código gerado e dados locais. Publicação e instalação limpa continuam pendentes.
Preparação gerencia MinGit 2.56.0.2, CMake 3.31.6, Ninja 1.13.1 e LLVM 22.1.8.
Arquivos oficiais têm SHA-256 fixo; caches são reaproveitados. SDK e submódulos
seguem a revisão fixada e os patches existentes. Downloads exigem consentimento.
Para C++ ausente, oferece o bootstrapper oficial do Build Tools 2022, verifica a
assinatura Microsoft e a versão e entrega a instalação interativa ao usuário.
Esse instalador pode pedir elevação; o aplicativo e as ferramentas locais não.
O bootstrapper foi baixado/verificado, mas não instalado nesta máquina já preparada.

Etapas exibidas vêm dos scripts reais: ferramentas, configure, codegen, reconfigure
e build. Cancelamento usa o Job Object existente; marcador impede aceitar build
interrompido como pronto. A trava de ambiente evita sessão e preparação simultâneas.
Importação interrompida preserva a cópia parcial e retoma a partir de bytes verificados.
Não há telemetria, upload automático, serviço de atualização ou desinstalador de dados.

Validações executadas:

- 16 testes / 64 asserções do modelo; 19 testes / 117 asserções de processos,
  sessão, caminhos, armazenamento e exclusividade, em Release.
- Importação real do pacote em ambiente novo com espaços, acentos e caracteres
  japoneses: 19 asserções passaram, incluindo instalação da biblioteca. Não utiliza executáveis de títulos do pacote.
- Dependência ausente recusou download sem consentimento; arquivo em cache
  corrompido foi rejeitado; save sintético foi preservado.
- Ferramentas portáteis foram baixadas e verificadas; segunda preparação reutilizou
  caches e SDK sem download. Preparação incremental de Castle Crashers concluiu
  as quatro etapas; 17559 concluiu após liberar uma DLL que estava em uso.
- Pacote `0.1.0-preview.2`: fontes copiadas para `.local/validation/Preparação João`,
  SDK público e submódulos obtidos do zero, 34 patches aplicados, arquivos de download
  verificados reaproveitados. Castle Crashers concluiu configure (422 s), codegen
  (150 s), reconfigure (6 s) e build (55 s), sem usar os executáveis da árvore principal.
  Isso usa MSVC já instalado nesta máquina, portanto não aprova máquina limpa.
- Iruka: `20261012T152401Z-regress-iruka`, L1–L5, 328 cores, diferença 5,222,
  zero erros/críticas/stubs. Timeout foi término diagnóstico, não compatibilidade.
- O mesmo iruka falhou antes de iniciar com caminho Unicode e passou a executar
  sem erros/stubs em `20261012T153052Z-unicode-after`, após manifesto UTF-8 por
  processo. Supervisor usa conversão explícita UTF-8. Não altera locale do Windows.
- Interface observada por Computer Use; o usuário interrompeu a automação por Escape.
  Isso não valida todos os controles ou escalas. Teclado/controle do assistente e
  inglês completo ainda exigem cobertura manual; o idioma das telas principais mudou.

Limites: falta executar uma primeira preparação completa no pacote em máquina limpa,
instalar MSVC ausente, validar atualização/rollback, todos os erros de espaço/permissão
e o ciclo completo com perfil, gameplay e save/load. Perfis originais ainda dependem
do host XUI contínuo; esse serviço não foi substituído por uma tela do assistente.
O candidato não comprova Linux, macOS, ARM64 nem suporte geral ao catálogo.

## Revisão da experiência inicial — 2026-10-12

Executado o ZIP preview.5 fora do repositório, em pasta temporária independente,
com PATH somente do Windows e sem argumentos apontando para a árvore de trabalho.
Dependências PE do aplicativo são apenas DLLs do Windows; não exige CRT separado.
Computer Use verificou a janela comum (1106 × 754 pixels), biblioteca sem títulos
instalados, navegação para Importar, seleção do `dash.xex` original no diálogo,
identificação Retail 17559 e estado Conteúdo incompleto. Em seguida, a seleção de
pasta importou os seis componentes restantes, registrou sete hashes/procedências
e apresentou Conteúdo completo e a ação de preparar. A troca para inglês mostrou
a navegação e a preparação em inglês. Nenhum jogo foi importado nesse ambiente.

Correções obtidas no teste visual: verificação inicial em segundo plano com janela
imediata; botões sem bordas residuais; atualização da aba selecionada; reaproveitar
consulta de hardware entre páginas; texto contido no cartão em estados longos.
34 testes existentes e focados passam, incluindo identidade, sessão, preservação
de originais, importação incremental e catálogo opcional. As 20.213 asserções
incluem o fuzz sintético do leitor de identidade, não milhares de títulos reais.

O launcher da release Pinyon Shift foi iniciado para inspeção visual, mas Computer
Use recusou acesso (`Computer Use was not approved to use pinyonshiftlauncher`).
Não afirmamos ter controlado sua janela; a análise funcional foi da implementação,
documentação e conteúdo real do ZIP, com licença e digest verificados.

Limites deste incremento: preparação completa pelo novo fluxo ainda não validada;
Windows limpo indisponível nesta execução (máquina de desenvolvimento com MSVC
instalado). O ensaio com PATH restrito valida abertura/importação independentes,
não substitui instalação limpa. STFS/ISO na interface, capas/metadados de todo o
catálogo e equivalência visual com produto final seguem pendentes. Nenhuma release
ou push foi feito; runtime, jogos e frente de avatares não foram recompilados.
Entrega consolidada: `0.1.0-preview.8`, código `67214d4`, ZIP SHA-256
`dc771db3bf13692272ab8d22a27248b7a02882b66236cd3c9b21e01f320f607f`.
Abertura vazia, navegação, biblioteca vazia, seleção/importação real de arquivo e
painel com os seis componentes faltantes foram novamente executados nesse ZIP
fora do repositório. Capturas em `.local/packages/x360-native-*-preview8.png`
na árvore principal, sem conteúdo proprietário na imagem. O teste adicional de
importação real do pacote passou com 19 asserções; o backend não mudou entre
preview.7 e preview.8. Nenhum ciclo de jogo novo é reivindicado por esse teste.

## Primeira preparação fora do repositório — 2026-10-12

O ZIP preview.12 foi extraído em `Temp/x360-first-use-74492181`, sem junctions,
SDK, ferramentas, código gerado ou DLLs copiados da árvore de desenvolvimento.
O aplicativo foi iniciado com PATH limitado ao Windows e sem variáveis de SDK,
CMake, LLVM ou Visual Studio. Pela interface, importou os originais da 17559,
obteve Git/CMake/Ninja/LLVM e SDK fixados, mostrou as etapas e compilou do zero:
configure 383 s, codegen 136 s, reconfigure 6 s, build 190 s. O supervisor e as
DLLs carregadas vieram dessa extração. O usuário confirmou intro com som; o menu
original apareceu, mas seus textos estavam incompletos.

Os bloqueios encontrados e corrigidos ficaram nos scripts de preparação:
isolamento de PSModulePath do PowerShell filho; extração por tar para caminhos
longos; `core.longpaths=true` apenas nos comandos Git locais; retomada do download
LLVM com hash integral obrigatório. O download final terminou normalmente; a
nova recuperação automática de conexão lenta ainda não teve aceite específico.

A falha de texto foi demonstrada no log: `media:\XenonJKLatin.xtt` retornou
`0xc000000f` 11.228 vezes na sessão `20261013T004336Z-session-0-dash_17559`.
O manifesto importava SegoeXbox-Light, mas omitia as fontes de idioma. Preview.13
associa XenonJKLatin, XenonCLatin e XenonSCLatin aos hashes do conjunto 17559
já existente. Não altera o renderizador nem distribui essas fontes. Os 34 testes
do frontend passam (20.213 asserções); o teste de importação do pacote real passa
com 23 asserções, incluindo detectar fonte ausente e recuperar só esse arquivo.

Preview.13 foi extraído novamente em `Temp/x360-first-use-4f785046`: importação
gráfica dos dez componentes, novos downloads/SDK e geração/compilação completas,
sem reaproveitar o ambiente preview.12. As quatro etapas terminaram com código 0
em 435/199/7/208 s. A ação Abrir dashboard iniciou a sessão
`20261013T011004Z-session-0-dash_17559`; a intro original retornou em 10,9 s e o
menu mostrou Open Tray, My Pins, Recent e os textos de instrução. Não houve falha
de abertura de fonte no relatório; os stubs de serviços de sistema permanecem.
A sessão fechou sem processo remanescente. O supervisor registra fechamento de
janela como cancelamento, código 1; isso não comprova Turn Off pelo menu original.
O aplicativo também foi fechado e reaberto: mostrou Pronta para iniciar, sem
nova preparação, com tamanho/data dos sete binários inalterados.
A ação Iniciar Xbox 360 abriu uma segunda sessão (`20261013T011156Z`); o log
confirma novamente retorno da intro e entrada na dashboard, seguida de fechamento
da janela. Não foi necessário preparar, baixar ou recompilar novamente.

Candidato final: `0.1.0-preview.13`, código `434ab65`, ZIP de 1.720.013 bytes,
SHA-256 `0aa1ec684ab9fb44e6f8b23080363bb40151e4d4050b7c44786abdc75ad11268`.
Capturas locais `x360-preview13-fonts.png`, `x360-preview13-ready.png` e
`x360-preview13-reopened.png`, junto do ZIP na pasta ignorada `.local/packages`.
Nenhum jogo foi importado neste ensaio: não aprova perfil, save/load ou catálogo.

Limites de dependências: Git/CMake/Ninja/LLVM ausentes foram obtidos e utilizados
pelo aplicativo. MSVC e Windows SDK já estavam instalados. Um processo de teste
sem localização de Build Tools retornou código 3 e a mensagem de dependência
ausente; isso não valida o instalador oferecido pela GUI. Não há aceite de Windows
limpo: `Get-VM` foi recusado por falta de permissão e nenhum ambiente limpo acessível
foi utilizado. Não houve instalação administrativa, publicação ou push.

## Biblioteca do pacote e navegação — 2026-10-12

O ensaio foi estendido no ambiente independente `Temp/x360-first-use-4f785046`:
pela interface, a pasta original extraída de Castle Crashers 0.0.1.3 foi
identificada e seus 708 arquivos foram importados. O jogo foi gerado/compilado
nesse ambiente, sem copiar recompilações ou DLLs da árvore de desenvolvimento.
As ferramentas obtidas na primeira preparação da dashboard foram reutilizadas.

Três bloqueios reais foram corrigidos na distribuição:

- O extrator de exports do CMake 3.31.6 falhava ao abrir um objeto existente cujo
  caminho absoluto tinha 263 caracteres. A lista `exports.def.objs` agora recebe
  caminhos estendidos do Windows quando necessário, após cada configure. O mesmo
  objeto falhou com caminho comum e passou com `\\?\`; não houve troca de símbolos
  ou supressão da etapa. Referência: `Source/bindexplib.cxx` da versão fixada do CMake.
- A cópia para a biblioteca também excedia MAX_PATH. O registro usa caminhos
  estendidos sem exigir alteração de política do Windows. Publica o header após
  concluir/verificar os assets e só então remove o marcador de preparação.
  Biblioteca parcial deixou de aparecer como pronta, inclusive ao selecionar o jogo.
- O supervisor exigia identidade física entre o XEX importado e o selecionado.
  Isso atendia à junction antiga, mas recusava uma instalação por cópia. Agora lê
  a identidade em `preparation.toml`, aceita somente o endereço registrado para
  aquele perfil na dashboard da sessão e verifica o SHA-256 exato. Outro caminho,
  outra dashboard, sinalizadores não suportados e bytes divergentes são recusados.
  Dados de lançamento e contexto continuam pelo handoff original.

O preview.18 foi extraído em `Temp/x360-library-preview18` e abriu o ambiente
externo já preparado, sem recompilar novamente. É um teste de retomada e execução
do pacote atualizado, não uma segunda preparação integral do zero dessa versão.
O build retomado do jogo (`desktop/167976046`) concluiu configure/codegen/
reconfigure/build em 5/21/5/19 s. Os logs anteriores preservam as falhas corrigidas.

Pela My Games original, o ciclo executado foi:
`20261013T015808Z-session-0-dash_17559` →
`20261013T015826Z-session-1-castle_crashers` →
`20261013T015854Z-session-2-dash_17559`.
O supervisor registrou correspondência com o XEX `825e9c8c…f5d9ad`, execução do
binário compilado no ambiente externo e pedido explícito de retorno à dashboard.
A dashboard recebeu 1.020 bytes, `kind=4`, `flags=0x100`, `dashboard_return=true`.
O usuário confirmou o ciclo; houve novo lançamento pela biblioteca na mesma
sessão (`session-3`). A mesma janela foi preservada e o retorno não repetiu a intro.
O primeiro jogo executou por 28,3 s, saída 0, sem erros/stubs; isso não valida
gameplay prolongado, save/load ou licença completa. O header não cria licenças.

A lentidão relatada na troca de telas veio da revalidação integral de conteúdo
em `Load`/`InspectSelected`: hashes dos imports e da biblioteca eram repetidos
a cada aba. A interface usa agora um resultado em memória, preenchido pelo worker
ao abrir/atualizar o ambiente e após operações. Trocar abas não relê o conjunto;
preparar/iniciar continuam verificando os arquivos em segundo plano antes do uso.
O usuário confirmou a resposta das telas e o ciclo na versão corrigida. Não há
benchmark de latência nem cache de integridade persistente que dispense validação.

Candidato: `0.1.0-preview.18`, código `9ecc073`, ZIP 1.740.090 bytes,
SHA-256 `8f1ed08b71b89c97a26b4238b81f0a7d62b9463d359c78b012379def85519d75`.
35 testes existentes/focados passam (20.226 asserções), incluindo cópia registrada,
recusa de caminho/versão divergentes, retomada e registro em caminho longo.
Capturas locais `x360-library-preview16-ready.png` (biblioteca registrada) e
`x360-preview18-game.png` (jogo executado pelo menu), em `.local/packages`.
Commits locais, sem push; runtime e frente de avatares preservados.

Limites mantidos: importação gráfica de jogos exige pasta extraída e preparação
compatível com a versão identificada. Importação direta STFS/LIVE/ISO não foi
implementada. Windows limpo e instalação de MSVC/Windows SDK ausentes continuam
pendentes; o ciclo com um título não aprova o restante do catálogo.

## Primeira utilização — verificação parcial em 13/10/2026

O teste em Windows limpo foi **dispensado pelo usuário durante esta rodada,
não executado e não aprovado tecnicamente**. Não foi encontrado Windows Sandbox
(`System32/WindowsSandbox.exe` ausente), serviços vmms/vmcompute ou instalação
de VMware/VirtualBox/QEMU nos locais e registros de instalação consultados.
Nenhuma VM limpa foi fornecida. Não foi habilitado hipervisor nem alterado o host.

Artefato verificado: `.local/packages/c10-provenance/x360-native-0.1.0-preview.7-windows-x64.zip`,
3.219.126 bytes, revisão `d87b5c95f50fafd8058c8ce6f50b844cc238e75a`.
SHA-256: `ff2dd3e8e1c8e9eaf901f774a34fd015596191d1dacb049e6e7ad85489d89cf1`.
O ZIP em `c10-provenance-repeat` tem o mesmo hash; os builds não foram repetidos.
`99fece1` altera somente `docs/LICENSE_AUDIT.md` e `docs/PROJECT_AUDIT.md`.
A alteração local de `window-route.ps1` acrescenta teclas ao auxiliar de teste;
os 49 arquivos cobertos pelos hashes finais dos patches do SDK coincidem com o lock.
Não houve mudança funcional demonstrada que exigisse outro pacote nesta rodada.

| Etapa | Resultado desta rodada |
|---|---|
| Extração do ZIP real | Passou no host de desenvolvimento; 399 arquivos conferidos contra `package.sha256`. |
| Abertura da GUI | Passou no host; janela real observada por Computer Use, início e configurações acessíveis. |
| Primeira configuração | Ambiente padrão e preferências criados automaticamente; 341 arquivos conferidos contra `payload.sha256`. Não houve edição manual de configuração. |
| Verificação/obtenção guiada das ferramentas ausentes | Não validada em Windows limpo. O host tem ferramentas de desenvolvimento e não serve como evidência dessa instalação. |
| Importação de dashboard original | Não repetida nesta rodada; nenhum conteúdo original foi incorporado ao ZIP. |
| Preparação AOT sem comandos manuais | Não executada nesta rodada. |
| Intro e dashboard | Não executadas nesta rodada. |
| Reabertura reutilizando preparação | Não validada; nenhuma preparação AOT foi produzida nesta rodada. |
| Importação/preparação de jogo suportado | Não executada nesta rodada. |
| Lançamento e retorno pelo fluxo original | Não repetidos nesta rodada; evidências anteriores não equivalem a instalação limpa. |

Evidências locais: `.local/validation/first-use-20261013/`, incluindo ZIP extraído,
`zip-hashes.txt`, `extraction.txt` e `environment.txt`. O ambiente criado está
dentro da extração e não recebeu caches, toolchains ou builds do checkout.
Isso isola os dados do aplicativo, mas não torna o Windows do host limpo.

Nenhum bloqueio de código foi reproduzido nas etapas executadas; nenhuma mudança
em jogos, avatares, dashboards, runtime ou instalação de ferramentas foi feita.
Para afirmar uso completo por leigos, ainda falta demonstrar a instalação guiada
de Build Tools/Windows SDK ausentes e toda a preparação/execução pela interface,
sem selecionar compiladores, executar comandos ou editar arquivos manualmente.
A dispensa encerra essa exigência operacional nesta rodada, sem criar evidência
de aprovação. Naquele ensaio não houve auditoria de licenciamento, push, Release
ou reescrita do histórico. A autorização posterior da primeira publicação pública,
incluindo o histórico existente, está registrada em `FIRST_RELEASE.md`.
