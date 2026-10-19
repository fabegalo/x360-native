# Handoff de pesquisa de recompilação (2026-10-17)

Este documento registra a pesquisa entregue em `x360-native-codex-handoff-2026-10-17.md`, conferida com o checkout local. É contexto para trabalho futuro, não autorização para implementar, publicar, fazer push ou criar Release. Para estado funcional, metodologia e decisões já documentadas, consulte [STATUS.md](../../STATUS.md), [CLAUDE.md](../../CLAUDE.md), [performance](../performance.md), [avatar-system](../avatar-system.md), [validação do Motocross](../reports/titles/validation-motocross-madness.md) e [biblioteca](../guides/universal-library.md); este handoff evita repetir esses históricos.

## Conferência do checkout

Na conferência, `HEAD` e `origin/main` estavam em `8c0f84b` (`test(avatar): fix shared-xam log checks and record the first run`), com commits predecessores `2bdf035`, `397a003` e `8acb08f` presentes. `external/rexglue-sdk` aparece modificado no `git status`; foi preservado sem alterações. O handoff de origem relata a main pública em `8c0f84b`, coerente com o checkout observado.

As invariantes resumidas no handoff coincidem com a documentação vigente: ReXGlue v0.10.0 fixado em `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, 86 patches ativos numerados até 0087 (0073 retirado), LLVM 22.1.8; Xenos/D3D12 é o caminho padrão e Native está suspenso. Não há suporte Vulkan certificado. Isolamento de módulos, recursos e hashes por versão de dashboard continua obrigatório. Os limites de perfis, saves, idioma e regra histórica de Games Played estão descritos nas fontes acima e não são revalidados por este registro.

## Estado relevante: serviço de avatar 17559

Os commits `2bdf035`–`8c0f84b` tornaram o serviço de avatar compartilhado, padrão ativo e vinculado aos arquivos verificados da dashboard 17559. `AvatarServiceArguments` recusa incompatibilidades de versão, XAM, catálogo, hash ou raiz antes de iniciar o título. Serviço e Guide compartilham uma única imagem XAM do sistema. A seção 17 de [avatar-system](../avatar-system.md#17-serviço-padrão-ligado-ao-perfil-da-dashboard-17559-2026-10-16) registra os testes e a execução curta: READY, `Initialize` e composição do Guide funcionaram; essa execução **não** acionou `GetAssets`, mipmaps ou o piloto em gameplay. AOT genérico ainda não recebe o serviço automaticamente.

No Motocross, as correções de terreno e piloto foram observadas em sessão Xenos conforme [validação](../reports/titles/validation-motocross-madness.md); a exceção de fetch tipo 0 continua específica ao manifesto do título. Permanecem sem certificação: ciclo completo do Motocross via My Games com avatar/Guide e retorno, `XamAvatarSetCustomAsset`, instalação do DLC Trick Jam e save/load comercial. A menção do handoff a ausência de benchmark/regressão total nesta rodada deve ser lida como limite de evidência, não como regressão ou ganho medido.

## Pesquisa externa recebida

As referências abaixo são pistas para estudo, não dependências aprovadas nem evidência local. Não transplantar código sem revisão de licença por arquivo, compatibilidade, contrato e testes. Em especial, o handoff classifica RaymanOriginsRecomp e UnleashedRecomp como GPL e ShiftGlue/Pinyon como especializados em Forza; números de FPS publicados não são resultados do x360-native. Xenolith, XenonRecomp, furqanagwan/rexglue-sdk, xbox, XenosRecomp, skate3recomp, hells-gate-recomp, MarathonRecomp, Xenia Canary/Edge e Xbox-BC-for-PC-Research podem informar investigação, respeitando licenças e escopo.

A pesquisa não identificou jogo comercial comprovado como funcional com ReXGlue puro e sem ajustes. O handoff cita The Darkness (51 entradas manuais/hooks), Perfect Dark Remaster (4 entradas/patches), vários ports adaptados, a demo Iruka com comparador qsort manual e Charlie Murder compilado que falha em `0x82163C60`. Esses exemplos não constituem classificação independente dos projetos; para evidência local e estado de Charlie Murder, veja [universal-library](../guides/universal-library.md).

## Candidata de pesquisa: AOT-Discovery

Após concluir a validação pendente do serviço/avatar e as regressões focadas pertinentes às mudanças gerais de heap/XEX, a próxima frente de pesquisa sugerida é melhorar descoberta AOT genérica e conservadora para funções sem `.pdata`, alvos em `.rdata` sem RTTI, thunks e jump tables, inclusive byte-indexadas. Primeiro auditar o que já existe em `src/codegen/{function_scanner,phase_discover,phase_gapfill,vtable_scanner,analyze}.cpp`: há padrões de switches, GapFill e tratamento do patch 0068; `functionPointerScan` está comentado e não deve ser ativado sem análise.

Corpus sugerido: Charlie Murder `0x82163C60`; Motocross (10 chamadas não resolvidas, switch de 16 entradas e 189/2141 candidatos aprovados pelo diagnóstico local); Forza AOT genérico (12 chamadas não resolvidas); Iruka (comparador qsort manual). Cada candidato precisa de procedência, limites e confiança explícitos. Não adivinhar endereços executáveis nem enfraquecer a semântica PPC ou funções já descobertas. Um eventual incremento requer testes sintéticos e corpus real, análise comparativa de codegen e regressões adequadas para dashboard 17559/6717 quando viável, Castle, Forza, Motocross, Iruka, perfis e saves. Registrar patch, hash/lock e limites somente se a implementação vier a ser autorizada.

Depois, e apenas se medições justificarem, investigar cache genérico de microcódigo Xenos inspirado no cache de `IM_LOAD` do ShiftGlue, com write-watch, invalidação correta, modo de verificação e comparação A/B de gameplay. A otimização 0073 foi revertida após piora de 1% low; não repetir sua hipótese sem evidência nova. Não reativar Native nem iniciar Vulkan nesta frente.

## Próxima prioridade

Fechar primeiro a validação delimitada do serviço de avatar 17559 no fluxo normal do Motocross (My Games, `GetAssets`/piloto e Guide, encerramento e retorno) e as regressões focadas pertinentes; só então considerar AOT-Discovery. Esta ordem respeita os limites registrados em [avatar-system §17](../avatar-system.md#17-serviço-padrão-ligado-ao-perfil-da-dashboard-17559-2026-10-16) e não autoriza iniciar sessões visuais ou longas sem autorização aplicável.
