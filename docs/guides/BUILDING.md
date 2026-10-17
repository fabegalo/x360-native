# Building and development

Guia técnico para Windows x64. Para conhecer o aplicativo e o fluxo gráfico,
comece pelo [README](../../README.md). As instruções já utilizadas no projeto foram
preservadas aqui; requisitos mínimos de hardware e Windows limpo não foram validados.

Há uma **prévia gráfica de preparação para Windows**, que abre mesmo sem conteúdo,
mostra os conjuntos cadastrados, verifica os componentes e inicia a sessão original.
Execute os comandos abaixo no PowerShell, na raiz do repositório. Depois do build, abra
`.local/build/win-amd64/src/desktop/Debug/x360-native.exe`.
Esta é a execução pela árvore de desenvolvimento; o pacote desktop leva sua
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
em [deps.lock.json](../../deps.lock.json). O script de ambiente configura MSVC e LLVM apenas
para o comando executado. Esse build produz as ferramentas e testes da plataforma;
os títulos são compilados separadamente, depois de preparar os arquivos locais.

## Onde colocar os arquivos

Conteúdo original, pacotes, fontes, saves e logs ficam em **.local/**, ignorado
pelo Git. O repositório não inclui jogos, dashboards ou módulos proprietários.
Mantenha os pacotes de origem e os registros de procedência e hashes.

### Jogos

Agrupe o conteúdo por jogo; mantenha discos e DLCs separados:

```text
.local/inputs/games/meu_jogo/
  source/       pacote original, ISO ou arquivos recebidos
  content/      pacote/demo extraído, quando presente
  disc-1/       default.xex e recursos do disco 1
  disc-2/       somente quando houver conteúdo do disco 2
  dlc/          pacotes adicionais, quando presentes
  metadata/     identidade, procedência e hashes
```

Quando o executável se chamar `default.xex`, ele deve ficar diretamente em
`content/` ou no diretório do disco correspondente. Não criar diretórios vazios.
Preserve todos os demais recursos e subdiretórios do jogo. Veja o
[layout de conteúdo e catálogo](content-layout.md).
Pacotes STFS (CON, LIVE, PIRS) podem ser extraídos com
`x360n-content extract <pacote> <destino>`. A ferramenta fica em
`.local/build/win-amd64/src/content/Debug/x360n-content.exe`.

A extração de ISO/GDFX está implementada: `x360n-content extract-disc <imagem.iso> <destino>`.
A interface gráfica também importa ISO e pacotes CON/LIVE/PIRS, conforme a auditoria E10.
Esse comando extrai a partição de jogo; não converte o título em um alvo executável.

**Copiar ou extrair arquivos não cria suporte ao jogo.** Ainda são necessários
identificação, análise, geração AOT, compilação e validação. A biblioteca gera
projetos genéricos automaticamente; perfis em `titles/` são overrides opcionais.
Consulte [Configurações por título](../../titles/README.md).

### Dashboards e módulos do sistema

O acervo usa `.local/inputs/dashboards/<versão>/<variante>/`; seu catálogo
local fica em `dashboards/metadata/organization/catalog.csv`. O perfil funcional
17559 mantém `.local/inputs/dashboards/17559/runtime/` como raiz de execução.

O [manifesto da 17559](../../titles/dash_17559/dash_17559_manifest.toml) registra os
caminhos e associações de `dash.xex`, `bootanim.xex`, `SegoeXbox-Light.xtt` e
módulos da interface. A dashboard usa rotinas XUI originais explicitamente
ligadas; o entrypoint do XAM não é executado e os serviços do SDK continuam ativos.
O uso experimental de rotinas originais para avatares está documentado em
[Sistema de avatares](../avatar-system.md).

`dashboards/resources/fonts/` é um acervo local, não um pacote global de fontes
para todas as versões. Cada versão usa exclusivamente seus recursos originais
identificados, sem fallback por nome, ordinal ou export para outra versão.
Saves e dados de usuário ficam sob
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
[dashboard](../../titles/dash_17559/dash_17559_manifest.toml) e do
[Castle Crashers](../../titles/castle_crashers/castle_crashers_manifest.toml).
Os scripts não fornecem nem preparam automaticamente esse conteúdo.

Para reproduzir a sessão de referência, compile os dois alvos em sequência.
As saídas do SDK são isoladas por build (C5); cada preparação possui recibo de
identidade e integridade (C2). Alterações nas dependências invalidam a reutilização:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-title.ps1 -Title castle_crashers
powershell -ExecutionPolicy Bypass -File scripts\build-title.ps1 -Title dash_17559
```

O script executa configure, build do gerador, codegen, reconfigure e build, interrompendo na primeira
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
com `--dash17559_boot_intro=false`; veja os detalhes em [Dashboard](../dashboard.md).

## Testes e diagnóstico

Após um build bem-sucedido, inspecione e execute somente os testes pertinentes.
`project` e `local.*` incluem testes gráficos; não são seleções padrão para
edições pequenas. Exemplo focado sem sessão de jogo:

```powershell
scripts\devshell.cmd ctest --preset win-amd64-debug -N -R "x360n[.]sha256"
scripts\devshell.cmd ctest --preset win-amd64-debug -R "x360n[.]sha256" --output-on-failure --timeout 30
```

Os testes `local.*` dependem dos arquivos de entrada correspondentes. Suítes
completas, testes visuais e sessões longas exigem necessidade e autorização
prévia; siga [CONTRIBUTING.md](../../CONTRIBUTING.md#choose-tests-that-match-the-change).
Não use binários antigos para validar um build que falhou.

A última suíte completa registrada em 13/10/2026, após C9/C12, executou 1.807 testes:
**1.802 aprovados, quatro ignorados e uma falha preexistente** em
`unit.Depfile escapes characters make treats as syntax`. Os 136 testes
`project` passaram. Consulte [a auditoria, E11](../reports/maintenance/PROJECT_AUDIT.md#e11-c9c12--ambiente-dos-testes-e-sessões-contínuas--13102026).
Esses resultados históricos não substituem uma nova validação após mudanças de código.

`x360n identify <arquivo> --json` registra identidade e SHA-256.
`x360n run <exe> [--label L] [--timeout S] [-- args...]` executa um alvo compilado
isoladamente; isso não valida a integração com My Games. O comando aceita saídas de
build, nunca executáveis em `.local/inputs`.

Cada execução gera `.local/evidence/runs/<UTC>-<label>/`, com logs e `report.json`
contendo identidade do executável, revisões e resultado. Hipóteses experimentais,
incluindo a região de memória específica da 17559, permanecem identificadas em
[Dashboard](../dashboard.md); não são contratos comprovados do console.
