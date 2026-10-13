# Aplicativo gráfico — candidato Windows x64

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