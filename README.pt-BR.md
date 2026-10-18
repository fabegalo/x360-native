<div align="center">
  <img src="docs/assets/x360-native-banner.png" alt="Ilustração abstrata de um console clássico se transformando em código nativo" width="100%" />

  # x360-native

  **Uma plataforma de pesquisa aberta para executar software do Xbox 360 por recompilação estática de PowerPC.**

  [![Windows x64](https://img.shields.io/badge/plataforma-Windows%20x64-107c10)](#estado-atual)
  [![Experimental](https://img.shields.io/badge/estado-experimental-orange)](#estado-atual)
  [![BSD-3-Clause](https://img.shields.io/badge/licença-BSD--3--Clause-blue)](LICENSE)

  [English](README.md) · [Português (Brasil)](README.pt-BR.md) · [Documentação](docs/README.md) · [Como contribuir](CONTRIBUTING.md)
</div>

> **Software experimental. A compatibilidade é limitada e varia conforme a versão exata da dashboard e do jogo.** Windows x64 é a única plataforma com evidências atuais de ponta a ponta. Nenhum jogo está certificado como plenamente funcional.

## O que é o x360-native?

O x360-native pesquisa uma forma diferente de executar software do Xbox 360 em um PC moderno. Em vez de traduzir instruções PowerPC enquanto o jogo está rodando, ele analisa o código original antes da execução, gera C++ e compila esse código em um executável nativo para o computador. Ao iniciar, o projeto ainda precisa de um runtime de compatibilidade que forneça serviços do console e traduza o trabalho gráfico do Xbox 360.

Em termos simples: **o código do processador do jogo é preparado antes da execução; o ambiente do console ainda é reproduzido enquanto ele roda.** Recompilação estática não significa “sem emulação”, e compilar um jogo não significa que ele seja jogável.

O projeto também busca executar a **dashboard original do Xbox 360** como um alvo próprio. A dashboard é a interface inicial do console: ela oferece o menu original e, quando há suporte, inicia jogos preparados pela área **My Games**. O aplicativo desktop ajuda a importar e preparar conteúdo local; ele não substitui a dashboard.

## O diferencial do projeto

- **Código nativo AOT:** as instruções PowerPC são traduzidas e compiladas antes da execução; o caminho principal não usa JIT nem interpretador de instruções durante o jogo.
- **A interface original faz parte do objetivo:** inicialização da dashboard, navegação, My Games, início de jogos e retorno são acompanhados como fluxos de compatibilidade explícitos.
- **Evidência antes de afirmações:** “compilado”, “inicia”, “renderiza” e “jogável” são estados diferentes. Os resultados correspondem ao conteúdo e à versão exatos testados.
- **Originais isolados por versão:** cada perfil de dashboard usa apenas os módulos e recursos originais correspondentes. Nome de arquivo ou export igual não comprova compatibilidade.
- **Desempenho com correção:** desempenho é um objetivo central, mas afirmações de velocidade exigem medições comparáveis e preservação de comportamento e fidelidade gráfica.

<a id="estado-atual"></a>

## Estado atual · 18 de outubro de 2026

A configuração principal demonstrada é **Windows x64 com o backend gráfico Xenos D3D12**. Outros sistemas operacionais e arquiteturas são objetivos, não plataformas com suporte de distribuição. As observações de compatibilidade se referem a revisões identificadas e testes locais; a prévia publicada não certifica automaticamente os mesmos resultados em qualquer Windows ou conjunto de arquivos.

| Alvo | O que foi demonstrado | O que ainda falta |
|---|---|---|
| **Dashboard Retail 17559 (Metro)** | Intro/áudio originais, navegação, My Games, perfis locais e retorno de jogos testados; smoke limitado em 18/10 registrou swaps da GPU. | Serviços e configurações ainda parciais. Swap não certifica quadro visível; um usuário local ativo no slot 0. |
| **Dashboard Retail 6717 (Blades)** | Compila; houve observações históricas de navegação. | Falta fonte/recurso correspondente e um smoke posterior sofreu crash. Alvo diagnóstico; sem sessão guiada de compatibilidade. |
| **Castle Crashers 0.0.1.3** | Início original pela dashboard 17559, observação de menu/entrada/renderização e retorno à dashboard. | Campanha, licença comercial e save/load do jogo não estão certificados. |
| **Forza Horizon 0.0.0.10** | Direção com entrada e áudio, seguida de retorno pelo Guide/Xbox Home à 17559. | Campanha e save/load não certificados; experimentos posteriores de renderer não herdam compatibilidade de produção. |
| **Motocross Madness 0.0.0.3** | Movimento/objetivos no tutorial histórico; correções de terreno/piloto, callback AOT restaurado e callback original de sign-in 17559; smoke limitado registrou swaps. | Fluxo completo My Games/perfil/Guide, fidelidade visual, corridas completas e save/load comercial sem aceite. |
| **Amostra Iruka / Dolphin (histórico)** | Verificações de regressão de renderização e animação. | Removida dos inputs locais pelo mantenedor em 18/10. Entrada, áudio, encerramento normal e sessões longas continuam sem validação. |
| **Charlie Murder 0.0.0.2** | Preparação AOT, registro das 31 DLLs nativas e roteamento My Games verificados. Sessão isolada alcançou HostLoader, RuntimeHost e bootstrap NetCF, retornando à dashboard. | Inicialização XNA, renderização, entrada, Guide, gameplay e saves sem aceite; bloqueio PTE/mapper NetCF documentado. |
| **Renderer Native D3D12** | Fluxos experimentais de dashboard/jogo foram observados. | Desenvolvimento suspenso; desempenho não aprovado, falta uma consulta de oclusão e Vulkan está indisponível. Resultados do Xenos não se aplicam ao Native. |

**Nenhum jogo listado está certificado como Jogável ou Plenamente Funcional.** Um início breve, build bem-sucedido ou chegada a um menu não comprova gameplay completo, progressão, saves ou compatibilidade geral. A [matriz de compatibilidade](docs/compatibility.md) e o [estado do projeto](STATUS.md), com datas, registram as evidências e ressalvas detalhadas.

O [fechamento da auditoria de 18/10](docs/reports/audit-closure-2026-10-18.md) registra medições incrementais, integridade dos produtos e aceites manuais restantes. Essa auditoria não executou jogos nem produziu nova Release.

### Avanços técnicos desde a preview.21

- **Launcher responsivo:** a listagem do mesmo acervo não calcula mais hashes de todos os arquivos, caindo de cerca de 84–85 segundos para 5,98 segundos. O encerramento da sessão é refletido sem reinspecionar o catálogo inteiro.
- **Preparação autenticada:** reutilização incremental segura dos produtos AOT/nativos e menos operações CMake/codegen. Um build completo medido da 17559 caiu de 703,12 para 552,65 segundos (21,4%); os no-ops dos quatro títulos melhoraram 31,7–33,0%. Alguns builds incrementais continuam demorados.
- **Preflight protegido:** no acervo testado com cache aquecido, a validação antes de criar o processo caiu de 31,27 para 19,35 segundos (17559), 9,27 para 7,74 (Charlie), 10,91 para 7,98 (Motocross) e 8,12 para 6,88 (Castle). SHA-256 integral segue obrigatório; a 17559 lê cerca de 22 GB.
- **Validação runtime/AOT:** smokes limitados registraram swaps da GPU na 17559, Motocross e Castle. Charlie retornou após o bootstrap NetCF, sem certificação XNA. Não há benchmark de primeira imagem visível ou gameplay.

Veja [desempenho do launcher](docs/reports/launcher-performance-2026-10-18.md), [benchmarks de preparação](docs/reports/preparation-startup-2026-10-18.md), [evidências do preflight](docs/reports/startup-preflight-2026-10-18.md), [limites funcionais de Charlie](docs/reports/charlie-functional-2026-10-18.md) e [diagnóstico NetCF](docs/reports/charlie-netcf-2026-10-18.md).


### O que significam os estados de compatibilidade

| Estado | Significado |
|---|---|
| **Identificado / Importado** | O conteúdo foi reconhecido ou adicionado à biblioteca local. |
| **Compilado** | O build nativo terminou para as entradas identificadas. O programa ainda pode falhar ao iniciar. |
| **Inicia** | O programa original começou a executar. Isso não comprova saída visual nem gameplay. |
| **Renderiza** | Uma cena visual foi observada. Entrada, áudio, progressão e persistência podem continuar ausentes. |
| **Em jogo** | Parte do gameplay foi alcançada ou observada. Isso não implica jogabilidade sustentada. |
| **Jogável** | Gameplay e progressão sustentados foram validados, incluindo fluxos pertinentes de entrada e persistência. Nenhum jogo acima possui essa certificação. |
| **Plenamente funcional** | Recursos e fluxos relevantes de ponta a ponta foram verificados. Nenhum jogo ou dashboard é declarado plenamente funcional. |

## Dashboards originais e isolamento de versão

O objetivo de longo prazo inclui gerações originais como Blades, NXE, Kinect e Metro, incluindo builds antigos e betas do acervo local. **O trabalho ativo de dashboard demonstrado é o Retail 17559.** A dashboard 6717 está em estado diagnóstico; as demais versões ficam pendentes até que suas entradas correspondentes e seu comportamento sejam validados.

Cada versão deve usar seu próprio `vk.xex`, módulos de perfil/login, XUI, skins, fontes e outros recursos específicos. O projeto registra procedência e identidade SHA-256 das entradas e recusa dependências ausentes ou incompatíveis, em vez de buscar arquivos silenciosamente em outra dashboard. A infraestrutura genérica do runtime pode ser compartilhada; arquivos originais específicos de uma versão não são compartilhados entre perfis.

O `vk.xex` 17559 identificado corresponde à versão Retail 17559. O `vk.xex` 13604 é incompatível e não pode substituí-lo. Consulte as [evidências da dashboard](docs/dashboard.md) e o [diagrama de isolamento](docs/assets/dashboard-isolation.svg).

## Veja a dashboard original

Estas são capturas reais de leitura da GPU em 1280×720, feitas em 14 de outubro de 2026 durante uma comparação de navegação da dashboard. A Home usa o backend padrão **Xenos / D3D12**. Games usa o backend **Native D3D12 experimental**, cujo desenvolvimento está suspenso e cujo desempenho não foi aprovado. As imagens mostram estados da dashboard, não gameplay nem um benchmark de desempenho, e não são uma comparação entre os dois backends.

| Dashboard 17559 · Home · Xenos | Dashboard 17559 · Games · Native experimental |
|---|---|
| ![Captura real da Home da Dashboard 17559](docs/assets/screenshots/dashboard-17559-home.png) | ![Captura real de Games da Dashboard 17559](docs/assets/screenshots/dashboard-17559-games.png) |

Os metadados e hashes estão em [`captures.json`](docs/evidence/native-n2-2026-10-14/captures.json); o conjunto completo de evidências fica em [`docs/evidence/`](docs/evidence/). A captura Games vem de `native-games.png`, onde as abas Games e o bloco My Games estão visíveis. O banner acima é arte conceitual original, não uma captura de tela nem uma afirmação de compatibilidade.

## Como funciona

![Arquitetura do x360-native: conteúdo local é recompilado estaticamente e executado com serviços de compatibilidade e tradução gráfica para o host](docs/assets/architecture.svg)

1. **Use seus próprios arquivos autorizados.** O repositório não fornece jogos, dashboards, firmware, fontes ou assets proprietários do Xbox.
2. **Identifique e prepare um conjunto específico.** As entradas são verificadas e associadas a hashes e a um perfil de título/dashboard.
3. **Gere e compile o código antes da execução.** O código PowerPC vira C++ e depois um build nativo. Isso não implementa automaticamente comportamento ausente de kernel, XAM, sistema de arquivos, áudio, entrada ou gráficos.
4. **Execute pelo runtime de compatibilidade.** O ReXGlue fornece boa parte da infraestrutura. Comandos e shaders da GPU Xenos ainda são traduzidos para uma API gráfica do host. O kernel Microsoft original completo, firmware e XAM não são inicializados.
5. **Valide o fluxo real.** A dashboard original pode iniciar um título preparado e tratar retornos demonstrados. Cada versão de dashboard e build de jogo precisa de evidência própria.

Mais detalhes: [arquitetura e decisões](docs/architecture.md), [biblioteca e preparação universal](docs/guides/universal-library.md), [layout de conteúdo](docs/guides/content-layout.md).

## Baixe e experimente a prévia

**[Baixe a prévia experimental para Windows x64](https://github.com/fabegalo/x360-native/releases/tag/v0.1.0-preview.22).** A v0.1.0-preview.22 é uma prévia técnica, não uma versão de compatibilidade geral. Contém o aplicativo desktop, não conteúdo proprietário do Xbox, binários de jogos nem a toolchain completa. Windows limpo continua sem aceite.

1. Baixe o ZIP Windows x64 e seu checksum SHA-256 da [Release preview.22](https://github.com/fabegalo/x360-native/releases/tag/v0.1.0-preview.22). Os arquivos automáticos “Source code” do GitHub não são o aplicativo.
2. Extraia o ZIP inteiro e execute `x360-native.exe`. Preserve a pasta `data/`: ela armazena o ambiente local, conteúdo importado, preparações e configurações.
3. No aplicativo, importe arquivos de dashboard e jogos que você esteja autorizado a usar. O programa os identifica e informa componentes ausentes ou sem suporte.
4. Prepare uma dashboard compatível e os jogos que deseja experimentar. Com consentimento, o app obtém ferramentas fixadas e compila localmente; componentes adicionais de C++ da Microsoft podem exigir o instalador oficial.
5. Inicie a dashboard original. No fluxo de referência demonstrado, abra **My Games** e execute a versão trial preparada de Castle Crashers. Use a opção **Return to Arcade** do jogo para voltar à dashboard.

A preparação pode falhar por conteúdo sem suporte, arquivos-fonte ausentes ou serviços de compatibilidade incompletos. **Importado** e **preparado** são estados da biblioteca, não promessas de que um jogo vai funcionar. O pacote não inclui conteúdo proprietário nem a toolchain completa. Consulte as [notas de instalação e validação da prévia](docs/guides/desktop-preparation.md) e o [registro de release](docs/reports/maintenance/FIRST_RELEASE.md).

## Roadmap

São objetivos, sem promessas de datas ou suporte concluído:

- Tornar confiáveis a primeira instalação e a preparação em um Windows limpo.
- Ampliar o suporte por versão às dashboards originais, começando pelas evidências e recursos que faltam em cada perfil.
- Melhorar perfis locais, Guide, serviços XAM e saves; validar save/load de jogos separadamente dos contratos de armazenamento.
- Investigar mais títulos de Xbox 360 com melhorias AOT genéricas e relatórios reproduzíveis por jogo.
- Medir CPU, memória, GPU e carregamento em cargas comparáveis; aceitar otimizações somente quando correção e fidelidade forem preservadas.
- Avaliar Linux, macOS e ARM64 como alvos separados, cada qual com validação própria de ponta a ponta.
- Explorar XNA/Xbox Live Indie Games e retrocompatibilidade com o Xbox original como pesquisa de longo prazo.

O progresso e os critérios de aceite estão no [checklist funcional](docs/CHECKLIST_FUNCIONAL_x360-native.md). Consulte também a [metodologia e o roadmap de performance](docs/performance.md).

## Perguntas frequentes

<details><summary><strong>“Nativo” significa que não há emulação?</strong></summary>
Não. O código PowerPC do jogo é compilado antes da execução, mas os serviços do console e o pipeline gráfico Xenos ainda precisam de implementações compatíveis. Os comandos e shaders da GPU são traduzidos para o host.
</details>

<details><summary><strong>Posso jogar todos os jogos de Xbox 360?</strong></summary>
Não. A compatibilidade com todo o catálogo é uma meta de longo prazo. Importar, identificar ou compilar um jogo não significa que ele inicia ou seja jogável. Consulte a matriz para a versão exata do título.
</details>

<details><summary><strong>Posso usar quaisquer arquivos de dashboard que encontrar?</strong></summary>
Não. Use arquivos obtidos legalmente e mantenha módulos e assets originais de cada dashboard no perfil de sua versão correspondente. Um componente de outra versão pode ser incompatível mesmo com nome ou exports iguais.
</details>

<details><summary><strong>Há suporte para Linux, macOS ou ARM?</strong></summary>
São objetivos. As evidências atuais de ponta a ponta são para Windows x64; builds em outras plataformas não comprovam suporte do runtime.
</details>

<details><summary><strong>Saves, Xbox Live, multiplayer e periféricos funcionam?</strong></summary>
Alguns contratos de perfil local e armazenamento foram implementados, mas save/load de jogos comerciais não foi demonstrado. Xbox Live, multiplayer online e periféricos especiais estão fora do escopo validado.
</details>

<details><summary><strong>Por que um jogo para depois de compilar?</strong></summary>
Ele pode depender de uma função PowerPC ausente, um export de XAM/kernel não implementado, um recurso não suportado ou comportamento do runtime ainda não reproduzido. Essas falhas ficam visíveis; um build bem-sucedido não é tratado como compatibilidade.
</details>

<details><summary><strong>O projeto distribui software do Xbox?</strong></summary>
Não. Forneça somente conteúdo que você esteja autorizado a usar. Não envie jogos, dashboards, XEX, fontes, firmware, saves, assets proprietários ou código gerado a partir de binários proprietários.
</details>

## Como contribuir

Contribuições são bem-vindas em documentação, relatos reproduzíveis, testes sintéticos, evidências de compatibilidade e correções focadas. Comece por aqui:

1. Leia [CONTRIBUTING.md](CONTRIBUTING.md), [STATUS.md](STATUS.md) e as notas pertinentes de [arquitetura](docs/architecture.md) ou [compatibilidade](docs/compatibility.md).
2. Ao relatar compatibilidade, informe versão/hash exatos do conteúdo, revisão do projeto, plataforma, passos de reprodução e logs sanitizados. Nunca anexe conteúdo proprietário ou dados pessoais.
3. Mantenha mudanças focadas. Para código, inclua um teste de regressão do contrato afetado quando viável e execute a validação mais restrita pertinente; compilação ou smoke curto não certificam gameplay.
4. Preserve o isolamento por versão, a semântica PPC/XAM e evidências medidas de desempenho. Veja as [expectativas para contribuições de performance](CONTRIBUTING.md#performance-changes).
5. Use mensagens Conventional Commits, por exemplo `docs(readme): clarify compatibility status`. Abra um pull request para `main`; não inclua arquivos de jogos ou binários gerados.

A documentação do projeto está principalmente em inglês e português atualmente; contribuições internacionais que melhorem clareza e documentação baseada em evidências são especialmente úteis. Para reportar uma vulnerabilidade, siga [SECURITY.md](SECURITY.md).

## Documentação

| Comece por | Conteúdo |
|---|---|
| [Estado do projeto](STATUS.md) | Evidências atuais, mudanças recentes e limites conhecidos |
| [Matriz de compatibilidade](docs/compatibility.md) | Resultados por dashboard e jogo |
| [Arquitetura](docs/architecture.md) | Recompilação estática, limites do runtime e processos |
| [Dashboard](docs/dashboard.md) | Interface original, módulos, perfis e evidências de início/retorno |
| [Guia de build](docs/guides/BUILDING.md) | Pré-requisitos de desenvolvimento e build local |
| [Biblioteca universal](docs/guides/universal-library.md) | Importação, identificação, preparação AOT e recibos |
| [Performance](docs/performance.md) | Política de medição e roadmap |
| [Índice da documentação](docs/README.md) | Documentos e relatórios de engenharia |

## Licença e independência

O código do projeto usa a licença [BSD-3-Clause](LICENSE). Dependências mantêm suas próprias licenças; consulte [avisos de terceiros](THIRD_PARTY_NOTICES.md) e a [auditoria de licenças](docs/reports/maintenance/LICENSE_AUDIT.md). O x360-native é independente e não é afiliado nem endossado pela Microsoft. Xbox e marcas relacionadas pertencem a seus respectivos titulares. A recompilação não concede direitos para redistribuir software ou assets originais.

Agradecemos ao [projeto ReXGlue](https://github.com/rexglue/rexglue-sdk) e seus contribuidores, e ao trabalho do Xenia que fundamenta o runtime de compatibilidade do ReXGlue.
