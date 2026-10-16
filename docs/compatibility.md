# Matriz de resultados

Consolidada em **2026-10-15**. Resultados por conteúdo e backend; não há título
certificado como plenamente funcional. Importar, gerar e compilar não comprovam
jogabilidade. Os links preservam hashes completos, runs, testes e falhas anteriores.

| Alvo / versão | Estado demonstrado em Windows x64 | Limites / evidência |
|---|---|---|
| Dashboard Retail 17559 / Xenos | Intro com áudio, Home/textos, navegação, My Games, perfis locais e retorno original de Castle/Forza | Serviços de sistema ainda parciais; um usuário ativo no slot 0. [Dashboard](dashboard.md), [Forza](validation-forza-horizon.md) |
| Dashboard Retail 6717 | Compilada; blades/navegação observadas historicamente | Fonte `XenonJKLatin.xtt` correspondente ausente; smoke posterior com acesso nulo. Somente diagnóstico, sem sessão guiada. [Auditoria](PROJECT_AUDIT.md) |
| Iruka / Dolphin, XEX `66ec34f2…cb61e31` | Renderização/animação; regressão L1–L5 | Entrada, áudio, execução longa e encerramento normal não validados. [Iruka](validation-iruka.md) |
| Castle Crashers 584108B7 / 0.0.1.3 | Menu, entrada e ciclo original 17559 ↔ jogo; cenas observadas no trial Native N3 | Sem campanha/save/load comercial ou licença completa certificados. [Castle](validation-castle-crashers.md), [N3](native-renderer-n3.md) |
| Motocross Madness 58410B0F / 0.0.0.3 | Tutorial com movimento e objetivos, serviço opt-in de avatares; integrado na main | Chão preto e piloto deformado; corrida completa/save/load não validados. [Avatares §15](avatar-system.md#15-integração-na-main-2026-10-13) |
| Forza Horizon 4D5309C9 / 0.0.0.10 / Xenos | My Games → condução com entrada/áudio → Guide/Xbox Home → 17559, mesma janela/perfil | Ciclo com recibos aprovado em 14/10; X2 em harness isolado, gate de produção não recertificado e navegação pós-retorno inconclusiva. Campanha/saves pendentes. [Forza](validation-forza-horizon.md), [X2](xenos-fetch-predication.md) |
| Charlie Murder 58410B83 / 0.0.0.2 | Importação LIVE, AOT compilado e lançamento pela My Games | Para na função AOT ausente `0x82163C60`; não jogável. [Biblioteca](universal-library.md) |

## Native D3D12

Backend experimental separado, **desenvolvimento suspenso**. Retail 17559 em
boot direto → Castle trial → retorno demonstrado em [N3](native-renderer-n3.md).
Forza passou do bloqueio PointList; [N4.1](native-renderer-n4-1.md) corrigiu cores
e [N4.2](native-renderer-n4-2.md) chegou à condução, com entrada confirmada.
Desempenho não aprovado, hitches e `EVENT_WRITE_ZPD` (0x5B) não implementado.
Vulkan indisponível. Compatibilidade e desempenho Xenos não são herdados.

## Importação, perfis e armazenamento

ISO/GDFX, CON/LIVE/PIRS e pastas extraídas podem entrar no catálogo sem perfil
manual. A preparação genérica tenta codegen/build com o SDK pinado; apenas
artefatos verificados entram na My Games. Versões e inventários permanecem
isolados. [Biblioteca universal](universal-library.md).

C7 demonstrou criação, seleção, troca, Sign-out e persistência de dois perfis
locais, um ativo no slot 0. C8 demonstrou saves por XUID/Title ID, flush e
recuperação entre processos via XAM/VFS. São contratos testados, **não aceite
de save/load comercial**. [C7](dashboard.md), [C8](save-storage.md).

## Referências de validação

As observações históricas usam o SDK `f5337cdc` e o conjunto de patches indicado
em cada relatório; não representar resultados antigos como execução do lock atual.
Integração da main: [22 testes focados e build Debug](integration-2026-10-15.md).
Suíte completa de 13/10: 1.802/1.807 aprovados, quatro skips e uma falha conhecida,
sem reexecução nesta consolidação. [Auditoria E11](PROJECT_AUDIT.md#e11-c9c12--ambiente-dos-testes-e-sessões-contínuas--13102026).

Testes PPC sintéticos do SDK e do projeto verificam tradução/execução de
instruções, não compatibilidade de jogos, XEX ou serviços de sistema. Os hashes,
comandos e resultados de cada campanha ficam nos relatórios correspondentes.
