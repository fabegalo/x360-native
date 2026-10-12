# x360-native

Plataforma experimental para executar a **dashboard original do Xbox 360** e
jogos recompilados para código nativo, usando [ReXGlue](https://github.com/rexglue/rexglue-sdk).
O primeiro ambiente funcional é Windows x86-64, com gráficos D3D12.

O fluxo já validado usa os menus e os pedidos de lançamento originais:

**Intro com imagem e som → dashboard 17559 → My Games → Castle Crashers → retorno à dashboard.**

A sessão mantém a mesma janela e não repete a intro ao voltar do jogo.
O projeto ainda está em desenvolvimento: não há uma distribuição pronta para
o usuário final nem compatibilidade geral com o catálogo do console.

[Estado detalhado](STATUS.md) · [Checklist oficial](CHECKLIST_FUNCIONAL_x360-native.md) ·
[Arquitetura](docs/architecture.md) · [Compatibilidade](docs/compatibility.md)

## O que funciona hoje

Resultados consolidados em **12 de outubro de 2026**, nos alvos e versões abaixo.
Compilar, abrir um menu e validar gameplay são etapas distintas.

| Alvo | Resultado observado | Limite atual |
|---|---|---|
| **Dashboard Retail 17559** | Menu Metro com atualização contínua, fonte original, navegação por teclado e controle. My Games enumera e lança Castle Crashers. | Guide, perfis e parte das configurações ainda estão incompletos. |
| **Intro da 17559** | Módulo original recompilado: animação, áudio e término pelo caminho original, seguido da dashboard. | Validada para esse conjunto; não é uma intro global para outras versões. |
| **Castle Crashers 0.0.1.3** | Inicia pela My Games original, chega ao menu, recebe teclado/controle e retorna pela opção do próprio jogo. | Gameplay completo, saves e load não estão validados nesse ciclo. |
| **Dashboard Retail 6717** | Interface Blades renderiza, troca de blade e move a seleção. | Sem textos: falta XenonJKLatin.xtt correspondente. Lançamento de jogos não validado. |
| **iruka / Dolphin** | Renderização e animação verificadas pela regressão L1–L5. | Alvo de referência técnica; não comprova compatibilidade de outros títulos. |

A transição da 17559 usa o pedido original de preservar a imagem, a sequência
de ajuste de gama do XAM e a drenagem do áudio pendente. O teste manual confirmou
o ciclo sem os cortes anteriores; ainda não há medição de equivalência com o console.

**Perfis locais continuam pendentes.** A cena original de GamerCard já é criada,
ligada ao HUD e passa pelo layout, mas ainda falta o host contínuo de atualização,
composição e entrada para apresentá-la. Criar perfil, entrar, sair e recuperar
os dados após reiniciar não estão validados. O usuário fixo do SDK não conta como
implementação desse fluxo. Evidências e bloqueios estão em [Dashboard](docs/dashboard.md).

## Como a plataforma funciona

O ReXGlue recompila estaticamente o código PowerPC de cada executável para código
nativo (**AOT**). O runtime reproduz os serviços de kernel, XAM, arquivos, entrada,
áudio e GPU Xenos necessários à execução. Xenia é referência técnica, não o executor.

Cada título e versão têm um manifesto com a identidade do XEX, a recompilação,
os recursos e as configurações correspondentes. O supervisor recebe o
pedido original da dashboard e seleciona o alvo AOT verificado; a interface de
lançamento continua sendo a My Games original.

**Todas as versões originais da dashboard, incluindo antigas e betas, são objetivo
permanente.** A 17559 é a primeira referência funcional. Trocar de versão exige
seu próprio perfil e suas dependências: não basta trocar o executável e reutilizar
silenciosamente XAM, intro ou fontes da 17559.

## Aplicativo de preparação para Windows

Já é gerado um candidato local `x360-native-0.1.0-preview.*-windows-x64.zip`, em
`.local/packages/`. Extraia e abra **x360-native.exe**. Escolha uma pasta separada
para o ambiente, importe seus conjuntos extraídos e clique em **Preparar**.
O jogo continua sendo iniciado pela **My Games original**.

O pacote inclui o aplicativo, o supervisor, dependências redistribuíveis e fontes
do projeto; não inclui jogos, dashboard, fontes Xbox ou código AOT desses títulos.
Git, CMake, Ninja, LLVM e SDK são preparados localmente, com versões verificadas
e consentimento para downloads. Se faltar C++/Windows SDK, o assistente oferece
o instalador oficial da Microsoft, que pode pedir autorização de administrador.
O uso normal e as ferramentas portáteis não exigem essa autorização.

Formatos guiados atuais: pasta extraída da Retail **17559** e pasta completa do
**Castle Crashers 0.0.1.3**. O assistente identifica o conjunto, verifica os hashes
e recusa componentes de outras versões. A pasta de saves pode ser escolhida
separadamente; trocar a seleção não apaga nem move os dados anteriores.

**Ainda é candidato de teste, sem release publicada ou aceite em máquina limpa.**
Perfis, save/load e atualização/rollback continuam pendentes. Não anuncie outras
plataformas como suportadas. [Evidências e limites](docs/desktop-preparation.md).

## Preparar o ambiente de desenvolvimento

Há uma **prévia gráfica de preparação para Windows**, que abre mesmo sem conteúdo,
mostra os conjuntos cadastrados, verifica os componentes e inicia a sessão original.
Depois do build abaixo, abra
`.local/build/win-amd64/src/desktop/Debug/x360-native.exe`.
Esta é a execução pela árvore de desenvolvimento; o candidato acima leva sua
própria cópia das fontes. O aceite completo para usuários leigos segue pendente.

- Windows 10 (1903 ou posterior)/11 x64. A execução de referência foi feita no Windows 11.
- Visual Studio 2022 Build Tools com C++, MSVC x64 e Windows SDK. Ambiente
  verificado: MSVC 14.44.35207, SDK 10.0.26100, CMake 3.31 e Ninja 1.12.
- Git e acesso à rede para preparar as dependências.
- LLVM/Clang **22.1.8**, baixado e verificado pelo bootstrap em `.local/toolchains`.
  Não exige instalação global do LLVM, Python ou privilégios administrativos.

Execute no PowerShell, na raiz do repositório:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\bootstrap.ps1
scripts\devshell.cmd cmake --preset win-amd64
scripts\devshell.cmd cmake --build --preset win-amd64-debug
```

O bootstrap prepara os submódulos, a toolchain e os patches locais do SDK fixados
em [deps.lock.json](deps.lock.json). O script de ambiente configura MSVC e LLVM apenas
para o comando executado. Esse build produz as ferramentas e testes da plataforma;
os títulos são compilados separadamente, depois de preparar os arquivos locais.

## Onde colocar os arquivos

Conteúdo original, pacotes, fontes, saves e logs ficam em **.local/**, ignorado
pelo Git. O repositório não inclui jogos, dashboards ou módulos proprietários.
Mantenha os pacotes de origem e os registros de procedência e hashes.

### Jogos

Para um novo título, crie uma pasta por versão, região ou disco:

```text
.local/inputs/titles/meu_jogo/
  source/       pacote original, ISO ou arquivos recebidos
  extracted/    default.xex e todos os recursos, na estrutura original
  metadata/     identidade, procedência e hashes
```

Quando o executável se chamar `default.xex`, ele deve ficar diretamente em
`extracted/`. Preserve todos os demais recursos e subdiretórios do jogo.
Pacotes STFS (CON, LIVE, PIRS) podem ser extraídos com
`x360n-content extract <pacote> <destino>`. A ferramenta fica em
`.local/build/win-amd64/src/content/Debug/x360n-content.exe`.

A extração de ISO/GDFX por `x360n-content extract-disc <imagem.iso> <destino>`
está em desenvolvimento local, ainda não consolidada na versão publicada.
Esse comando extrai a partição de jogo; não converte o título em um alvo executável.

**Copiar ou extrair arquivos não cria suporte ao jogo.** Ainda são necessários
identificação, perfil em `titles/`, análise, geração AOT, compilação e validação.
Consulte [Configurações por título](titles/README.md).

### Dashboards e módulos do sistema

O acervo usa `.local/inputs/dashboard/versions/<versão>/<variante>/`; seu catálogo
local fica em `dashboard/metadata/organization/catalog.csv`. O perfil funcional
17559 mantém `.local/inputs/dashboard/17559/` como raiz de execução.

O [manifesto da 17559](titles/dash_17559/dash_17559_manifest.toml) registra os
caminhos e associações de `dash.xex`, `bootanim.xex`, `SegoeXbox-Light.xtt` e
módulos da interface. O XAM original é usado apenas pelas rotinas XUI explicitamente
ligadas; seu entrypoint não é executado e os serviços do SDK continuam ativos.

`dashboard/resources/fonts/` é um acervo local, não um pacote global de fontes
para todas as versões. Compartilhe recursos somente quando sua identidade ou
compatibilidade estiver demonstrada. Saves e dados de usuário ficam sob
`.local/user/<perfil-de-execução>/`, conforme o manifesto.

### DLC e atualizações

DLC e addons (`00000002`) pertencem à pasta do jogo: pacote em
`source/<TitleID>/00000002/`, extração em `dlc/<nome>/` e hashes em
`metadata/dlc_<nome>.sha256`. Preserve ZIP/7z recebidos em `source/archives/`.
Uma atualização de título (`000B0000`) pode mudar o executável: verifique sua
identidade e prepare o alvo AOT correspondente, em vez de tratá-la como DLC.

O SDK procura DLC extraída em
`<user_data_root>/0000000000000000/<TitleID>/00000002/<pacote>/`.
**Esse caminho ainda não foi validado com uma DLC real.**

## Compilar e executar a sessão 17559

Os arquivos locais precisam corresponder aos manifestos da
[dashboard](titles/dash_17559/dash_17559_manifest.toml) e do
[Castle Crashers](titles/castle_crashers/castle_crashers_manifest.toml).
Os scripts não fornecem nem preparam automaticamente esse conteúdo.

Compile os dois alvos em sequência; os builds compartilham saídas do SDK:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-title.ps1 -Title castle_crashers
powershell -ExecutionPolicy Bypass -File scripts\build-title.ps1 -Title dash_17559
```

O script executa configure, codegen, reconfigure e build, interrompendo na primeira
falha. Os executáveis ficam em `.local/build/titles/<nome>/`.

Inicie a sessão pelo supervisor:

```powershell
$x360n = '.\.local\build\win-amd64\src\supervisor\Debug\x360n.exe'
& $x360n doctor
& $x360n session titles\dash_17559\dash_17559_manifest.toml --timeout 900
```

Após a intro, use **My Games → Castle Crashers**. Para voltar, use **Return to
Arcade** no menu do jogo. Fechar a janela encerra a plataforma. O timeout do
exemplo é de 900 segundos por título; `--timeout 0` desativa esse limite.

A navegação foi confirmada com controle e teclado (`Shift` + setas na dashboard).
A inicialização direta continua disponível para diagnóstico, executando o alvo
com `--dash17559_boot_intro=false`; veja os detalhes em [Dashboard](docs/dashboard.md).

## Testes e diagnóstico

Após um build bem-sucedido, execute o subconjunto pertinente:

```powershell
scripts\devshell.cmd ctest --preset win-amd64-debug -L project
scripts\devshell.cmd ctest --preset win-amd64-debug -R "local[.]"
```

Os testes `local.*` dependem dos arquivos de entrada correspondentes. A suíte
completa pode ser executada com `ctest --preset win-amd64-debug -j 16` pelo mesmo
`devshell.cmd`; não use binários antigos para validar um build que falhou.

A consolidação de 12/10/2026 registrou **42 testes do SDK e 40 do supervisor
aprovados**, regressão iruka L1–L5 e o ciclo real com Castle Crashers. Esses são
resultados dos grupos exercitados, não uma aprovação recente da suíte inteira.
Há uma falha conhecida no teste upstream `unit.Depfile escapes characters make treats as syntax`;
consulte [STATUS.md](STATUS.md) para o contexto da execução.

`x360n identify <arquivo> --json` registra identidade e SHA-256.
`x360n run <exe> [--label L] [--timeout S] [-- args...]` executa um alvo compilado
isoladamente; isso não valida a integração com My Games. O comando aceita saídas de
build, nunca executáveis em `.local/inputs`.

Cada execução gera `.local/runs/<UTC>-<label>/`, com logs e `report.json`
contendo identidade do executável, revisões e resultado. Hipóteses experimentais,
incluindo a região de memória específica da 17559, permanecem identificadas em
[Dashboard](docs/dashboard.md); não são contratos comprovados do console.

## Próximas etapas e documentação

O [checklist funcional](CHECKLIST_FUNCIONAL_x360-native.md) orienta os incrementos:
interface original de perfis e Guide, persistência, ampliação da compatibilidade
por versão e distribuição com preparação guiada. Windows, Linux e macOS são
objetivos, com validação separada por sistema e arquitetura; hoje a referência
funcional é Windows x86-64.

Títulos XNA/Xbox Live Indie Games exigem uma categoria própria e runtime gerenciado;
não são tratados como jogos PowerPC nativos comuns. Esse suporte segue pendente.
Xbox Live, multiplayer online e periféricos especiais também não estão validados.

| Documento | Conteúdo |
|---|---|
| [STATUS.md](STATUS.md) | Estado consolidado, resultados e desvios conhecidos |
| [Checklist funcional](CHECKLIST_FUNCIONAL_x360-native.md) | Requisitos, prioridades e critérios de aceite |
| [Arquitetura](docs/architecture.md) | Recompilação, runtime e decisões técnicas |
| [Compatibilidade](docs/compatibility.md) | Critérios e evidências por alvo |
| [Dashboard](docs/dashboard.md) | Evolução da 17559 e da 6717, transições e XUI |
| [Castle Crashers](docs/validation-castle-crashers.md) / [iruka](docs/validation-iruka.md) | Identidade dos arquivos e validações dos títulos |
| [titles/](titles/README.md) | Organização dos manifestos e configurações por executável |

## Licenças

A licença própria deste repositório ainda não foi definida. ReXGlue e suas
dependências mantêm suas licenças e avisos de terceiros; consulte os respectivos
projetos. O repositório não concede direitos sobre jogos, fontes ou recursos
originais do Xbox 360. Binários proprietários e código AOT gerado a partir deles
não fazem parte da publicação.
