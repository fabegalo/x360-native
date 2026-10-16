# STATUS

Estado consolidado em **2026-10-15**, após a integração `1844245`.
Este arquivo resume o estado atual; os relatórios vinculados preservam datas,
falhas iniciais, hashes, comandos e limites de cada validação. Resultados de uma
etapa histórica não certificam automaticamente o build atual nem outros títulos.

## Plataforma e dependências

Windows x64/D3D12 é o ambiente demonstrado. Linux, macOS, ARM64 e Windows limpo
continuam sem aceite funcional completo. ReXGlue v0.10.0 permanece fixado em
`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, LLVM 22.1.8, **76 patches ativos**
no [lock](deps.lock.json); numeração até 0077, com 0073 retirado.
O submódulo modificado pelos patches/materialização do libmspack é esperado.
Nunca substituir módulos ou fontes originais entre versões de dashboard.

## Funciona, dentro dos limites demonstrados

| Área | Evidência | Limites |
|---|---|---|
| Retail 17559 | Intro original com áudio, Home, fontes, navegação, My Games e retorno sem repetir intro | Não executa o firmware/XAM inteiro; configurações e serviços ainda parciais. [Dashboard](docs/dashboard.md) |
| Perfis locais C7 | Criação de Player1/Player2, seleção, troca, GamerCard, Sign-out e persistência pela UI original | Um usuário ativo no slot 0; sem Xbox Live, usuários simultâneos ou Guide completo. [C7.7/C7.8](docs/dashboard.md#031-múltiplos-perfis-locais-pela-interface-original-c77-2026-10-13) |
| Saves C8 | Isolamento por XUID/Title ID, flush e recuperação em processos independentes pelo caminho XAM → VFS → host | Testes de contratos; **save/load de progresso comercial ainda não comprovado**. [Armazenamento](docs/save-storage.md) |
| Castle Crashers 0.0.1.3 | Ciclo original 17559 → My Games → jogo → 17559, mesma janela; entrada e cenas observadas no escopo registrado | Trial local; campanha, licença completa e save/load não certificados. [Histórico](docs/validation-castle-crashers.md), [Native N3](docs/native-renderer-n3.md) |
| Forza Horizon 0.0.0.10 | Ciclo My Games → condução → Guide/Xbox Home → 17559 com recibos em 14/10; perfil/entrada/áudio e retorno registrados | Campanha e saves não certificados. X2 repetiu o ciclo em harness isolado, sem recertificar o gate de preparação de produção. [Forza](docs/validation-forza-horizon.md), [X2](docs/xenos-fetch-predication.md) |
| Motocross Madness | Tutorial com movimento/objetivos e serviço opt-in de avatares, integrado na main | Chão preto, piloto deformado; corridas, animações e save/load pendentes. [Avatares §15](docs/avatar-system.md#15-integração-na-main-2026-10-13) |
| Iruka | Referência técnica com renderização/animação e regressão L1–L5 | Entrada, áudio, encerramento normal e sessões longas não validados. [Iruka](docs/validation-iruka.md) |
| Retail 6717 | Build e observações históricas de blades/navegação | Fonte correspondente ausente e smoke posterior com crash; somente diagnóstico, sem sessão guiada. [Auditoria](docs/PROJECT_AUDIT.md) |
| Biblioteca e preparação | Importação ISO/GDFX, STFS e pasta; catálogo persistente; geração/build AOT genéricos e recibos | Importado/compilado não significa jogável. Charlie Murder para em função AOT ausente. [Biblioteca](docs/universal-library.md) |
| Desktop | Interface Windows redesenhada, biblioteca, diagnóstico, seleção global de renderer e preferências | Windows limpo, acessibilidade e aceite completo de primeira utilização pendentes. [Interface](docs/desktop-ui.md), [Preparação](docs/desktop-preparation.md) |

## Renderização e performance

**Xenos é o padrão; Native está suspenso por decisão do mantenedor.**
Patches 0076/0077 corrigem resolves vazios e fetches comprovadamente inativos,
com 204 e 2.328 checks sintéticos respectivamente. Forza chegou à condução sem
os erros diagnosticados; isso não estabelece ganho global de FPS.
[Evidências Xenos](docs/xenos-fetch-predication.md).

Native D3D12 permanece experimental. N3 demonstrou o ciclo Castle; N4.1 corrigiu
cores/bindless e N4.2 reduziu custo de PSOs e esperas de views inativas. Forza
chegou à condução, mas desempenho não aprovado e `EVENT_WRITE_ZPD` (0x5B)
continua não implementado. Vulkan indisponível, com recusa explícita.
[N4.1](docs/native-renderer-n4-1.md), [N4.2](docs/native-renderer-n4-2.md).

Instrumentação opcional/desligada normalmente. P2 não demonstrou ganho ou
sobrecusto causal nos jogos; P3 reverteu 0073 por piora de 1% low. Preservar os
resultados negativos e comparar cenas/condições equivalentes.
[Metodologia e roadmap](docs/performance.md), [P1](docs/performance-results-2026-10-14.md),
[P2](docs/performance-p2-2026-10-14.md), [P3](docs/performance-p3-2026-10-14.md).

## Validação e distribuição

Integração de Native/desktop/Xenos sem reescrita: build Debug completo e **22
testes focados aprovados**, com falhas iniciais e repetições necessárias descritas
no [relatório de integração](docs/integration-2026-10-15.md). Não houve nova
sessão de jogo nem ampliação de aceite de performance nessa integração.

Última suíte completa registrada, em 13/10/2026: **1.807 testes, 1.802 aprovados,
4 skips e 1 falha conhecida** em `unit.Depfile escapes characters make treats as syntax`;
136 testes project passaram. A suíte não está inteiramente verde. Esses totais
são históricos, não a contagem de uma execução nova.
[Auditoria E11](docs/PROJECT_AUDIT.md#e11-c9c12--ambiente-dos-testes-e-sessões-contínuas--13102026).

Prévia pública mais recente conferida em 15/10: **v0.1.0-preview.20**, corrigida.
A main inclui trabalho posterior que não foi publicado nesse pacote. Nenhuma
nova Release resulta desta limpeza. [Notas](docs/releases/0.1.0-preview.20.md),
[primeira Release](docs/FIRST_RELEASE.md), [automação](docs/release-automation.md).

## Prioridades e preservação

Fortalecer o Xenos conforme gargalos demonstrados, concluir os fluxos offline e
save/load comercial, e validar primeira utilização em Windows limpo. São
pendências, não autorização para iniciar novos trabalhos. Cobertura completa no
[checklist](docs/CHECKLIST_FUNCIONAL_x360-native.md).

Originais, catálogos, perfis/saves, logs/evidências, SDK pinado, patches e backups
de finalidade incerta devem ser preservados. Build/codegen são regeneráveis,
mas sua remoção exige nova preparação antes de executar. Regras operacionais em
[CLAUDE.md](CLAUDE.md); comandos em [BUILDING.md](docs/BUILDING.md).
