# Preparação gráfica — candidato Windows

Incrementos locais de 2026-10-12 para os §§15–18. Referência de experiência:
[Pinyon Shift](https://github.com/arcanite24/pinyon-shift#play): aplicativo e
preparação local, preservando o runtime e a interface original do nosso projeto.

`scripts/package-desktop.ps1` gera em `.local/packages/` um ZIP Windows x64 com
`x360-native.exe`, `x360n.exe`, CRT Release, licenças e snapshot commitado das fontes.
O script recusa jogos, fontes, XEX, código AOT gerado e dados locais. O candidato
não é publicado automaticamente. Fontes e binários do aplicativo devem corresponder
ao commit exportado. Nenhuma release está aprovada pelo §18.

Na primeira abertura, o aplicativo prepara uma pasta de ambiente, que pode ser
alterada em Configurações. Os arquivos do pacote
são verificados antes da cópia; outra versão existente não é sobrescrita. Dados
ficam fora do pacote. Remover o aplicativo não remove o ambiente. A opção de pasta
de saves grava somente a seleção; dados anteriores não são movidos nem apagados.

Revisão de interface após feedback do usuário: uma ação principal por estado,
etapas Conteúdo → Preparação → Dashboard, configurações e diagnóstico separados.
A seleção principal contém dashboards; importar Castle Crashers retorna ao fluxo
da dashboard que depende dele. A primeira abertura do pacote oferece ambiente
padrão em `%LOCALAPPDATA%/x360-native/environment`; outra pasta pode ser escolhida.
Referência de fluxo conferida no `launcher/PinyonShift.Launcher/MainWindow.xaml`
do Pinyon Shift (estado, ação principal e opções secundárias). Layout próprio;
sem copiar arte do projeto ou substituir telas do Xbox. Revisão compilada;
apresentação visual e navegação ainda aguardam teste, pois Computer Use foi interrompido.

O catálogo é declarativo por versão. Importação aceita pastas extraídas da 17559
e Castle Crashers 0.0.1.3, com detecção pelo XEX e hashes de todos os componentes
declarados, incluindo os 707 assets do jogo. O inventário corresponde sem diferenças
ao registro da extração já existente. Conjunto misturado é ambíguo, não é combinado.
STFS/ISO diretamente na interface continuam pendentes. A 6717 aparece com seu
bloqueio de fonte; não herda recursos da 17559.

A preparação registra as dependências na pasta de conteúdo do SDK da dashboard
selecionada. Antes faltava esse passo em ambientes novos, embora a instalação
manual antiga funcionasse. Metadados declarados vêm do LIVE já inventariado;
todos os assets importados são verificados e copiados antes de publicar o cabeçalho
de enumeração. O cabeçalho coincide com o registro funcional existente, sem trailer
de licença inventado. My Games continua enumerando e lançando pelo código original.
Uma instalação existente diferente é preservada e impede a preparação silenciosa.

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
