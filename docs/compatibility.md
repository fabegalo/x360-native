# Matriz de resultados

Consolidada em **2026-10-18**. Resultados por conteúdo e backend; não há título
certificado como plenamente funcional. Importar, gerar e compilar não comprovam
jogabilidade. Os links preservam hashes completos, runs, testes e falhas anteriores.

| Alvo / versão | Estado demonstrado em Windows x64 | Limites / evidência |
|---|---|---|
| Dashboard Retail 1888 / Xenos | Intro original com animação/som e transição à Blades; textos, Games → Media → System e Console Settings com teclado; controle físico confirmado pelo mantenedor | Release próprio, launcher e snapshot isolado. Guide/perfis/serviços/jogos sem aceite; recurso pt-BR ausente. [1888](reports/dashboard-1888-2026-10-18.md), [intro](reports/dashboard-1888-intro-2026-10-18.md) |
| Dashboard Retail 17559 / Xenos | Intro com áudio, Home/textos, navegação, My Games, perfis locais e retorno original de Castle/Forza | Serviços de sistema ainda parciais; um usuário ativo no slot 0. [Dashboard](dashboard.md), [Forza](reports/titles/validation-forza-horizon.md) |
| Dashboard Retail 6717 | Compilada; blades/navegação observadas historicamente | Fonte `XenonJKLatin.xtt` correspondente ausente; smoke posterior com acesso nulo. Somente diagnóstico, sem sessão guiada. [Auditoria](reports/maintenance/PROJECT_AUDIT.md) |
| Iruka / Dolphin, XEX `66ec34f2…cb61e31` (histórico) | Renderização/animação; regressão L1–L5 | Removido dos inputs pelo mantenedor em 18/10, sem restauração. Entrada, áudio, execução longa e encerramento normal não validados. [Iruka](reports/titles/validation-iruka.md) |
| Castle Crashers 584108B7 / 0.0.1.3 | Menu, entrada e ciclo original 17559 ↔ jogo; cenas observadas no trial Native N3 | Sem campanha/save/load comercial ou licença completa certificados. [Castle](reports/titles/validation-castle-crashers.md), [N3](reports/graphics/native-renderer-n3.md) |
| Motocross Madness 58410B0F / 0.0.0.3 | Tutorial histórico; correções de terreno/piloto em 16/10. Callback `0x82B36410`, dependência 17559 e perfil existente em cópia verificados em 18/10 | Callback de sign-in corrigido, sem novo teste pela interface. Corrida completa, fidelidade e save/load pendentes. [Avatares](avatar-system.md#16-construtores-estáticos-do-xam-17559-2026-10-16), [auditoria](reports/audit-closure-2026-10-18.md) |
| Forza Horizon 4D5309C9 / 0.0.0.10 / Xenos | My Games → condução com entrada/áudio → Guide/Xbox Home → 17559, mesma janela/perfil | Ciclo com recibos aprovado em 14/10; X2 em harness isolado, gate de produção não recertificado e navegação pós-retorno inconclusiva. Campanha/saves pendentes. [Forza](reports/titles/validation-forza-horizon.md), [X2](reports/graphics/xenos-fetch-predication.md) |
| Charlie Murder 58410B83 / 0.0.0.2 | Recibos atuais e 31 DLLs verificados; pedido My Games executou HostLoader/RuntimeHost/NetCF e retornou sozinho. Registro duplicado por tipo divergente corrigido genericamente | Bootstrap XNA incompleto; renderização, entrada, Guide/Xbox Home, gameplay e saves não aprovados. [Teste manual e limites](reports/charlie-functional-2026-10-18.md) |
| Minecraft 584111F7 / 0.0.0.1 / genérico / Xenos D3D12 | 27/10, build com 0212: tutorial 3D, deslocamento e extração de madeira na hotbar; 120 s sem crash usando configuração global. Build 0210 também mostrou pesca | Cópia completa isolada do console, encerramento no limite. Versão de avaliação explícita; save/reload e versão integral sem aceite. [Evidência](reports/titles/generic-forza-horizon-2-minecraft-2026-10-26.md) |
| Forza Horizon 2 4D530AA4 / 0.0.0.12 / genérico / Xenos D3D12 | 27/10, build com 0212: introdução e condução “Dirija até o Festival”, HUD/minimapa, 138–206 km/h e distância 9,3 → 6,9 km; 180 s sem crash | Console completo isolado, encerramento no limite. Campanha, save/reload e Native sem aceite; a lista nula anterior não reapareceu nesta tentativa, sem causa demonstrada. [Evidência](reports/titles/generic-forza-horizon-2-minecraft-2026-10-26.md) |

## Native D3D12

Backend experimental separado, **desenvolvimento suspenso**. Retail 17559 em
boot direto → Castle trial → retorno demonstrado em [N3](reports/graphics/native-renderer-n3.md).
Forza passou do bloqueio PointList; [N4.1](reports/graphics/native-renderer-n4-1.md) corrigiu cores
e [N4.2](reports/graphics/native-renderer-n4-2.md) chegou à condução, com entrada confirmada.
Desempenho não aprovado, hitches e `EVENT_WRITE_ZPD` (0x5B) não implementado.
Vulkan indisponível. Compatibilidade e desempenho Xenos não são herdados.

## Importação, perfis e armazenamento

ISO/GDFX, CON/LIVE/PIRS e pastas extraídas podem entrar no catálogo sem perfil
manual. A preparação genérica tenta codegen/build com o SDK pinado; apenas
artefatos verificados entram na My Games. Versões e inventários permanecem
isolados. [Biblioteca universal](guides/universal-library.md).

C7 demonstrou criação, seleção, troca, Sign-out e persistência de dois perfis
locais, um ativo no slot 0. C8 demonstrou saves por XUID/Title ID, flush e
recuperação entre processos via XAM/VFS. São contratos testados, **não aceite
de save/load comercial**. [C7](dashboard.md), [C8](save-storage.md).

## Referências de validação

Auditoria atual: [builds incrementais, integridade e contratos sem jogos](reports/audit-closure-2026-10-18.md).
Os resultados abaixo conservam suas datas; não foram reexecutadas suítes completas.

As observações históricas usam o SDK `f5337cdc` e o conjunto de patches indicado
em cada relatório; não representar resultados antigos como execução do lock atual.
Integração da main: [22 testes focados e build Debug](reports/maintenance/integration-2026-10-15.md).
Suíte completa de 13/10: 1.802/1.807 aprovados, quatro skips e uma falha conhecida,
sem reexecução nesta consolidação. [Auditoria E11](reports/maintenance/PROJECT_AUDIT.md#e11-c9c12--ambiente-dos-testes-e-sessões-contínuas--13102026).

Testes PPC sintéticos do SDK e do projeto verificam tradução/execução de
instruções, não compatibilidade de jogos, XEX ou serviços de sistema. Os hashes,
comandos e resultados de cada campanha ficam nos relatórios correspondentes.
