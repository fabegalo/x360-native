# STABLE-02 — recertificação de 27/10/2026

Baseline executada: main local/remota
`f84436df94ab2e17275fa4272107cc1aa4c400f3`, confirmada por `git fetch origin main`.
Windows x64/Release/Xenos D3D12/Retail 17559, SDK
`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, 201 patches ativos.
Esta revisão já contém `c788f9d`; o FAIL da STABLE-01 pertence a `9abb880`.
A [matriz nova](matrix-stable-02.json) identifica cada resultado da revisão
executada; a matriz anterior e suas falhas foram preservadas.

**Certificação v1.0.0 recusada**, somente pelos quatro aceites reais NOT RUN.
Nenhum contrato automatizado falhou nesta execução.

| Verificação atual | Resultado |
|---|---|
| SDK pinado, 201 patches, 271 arquivos finais e alterações dos submódulos | PASS |
| Build Release dos alvos supervisor/runtime/desktop exigidos pelo gate | PASS |
| 37 contratos obrigatórios, executados do zero | 37 PASS; 72,22 s |
| Relançamento com preferências que falhava na STABLE-01 | PASS; 2,374 s |
| 80 transições, estados e handoffs intactos | PASS; 28,865 s |
| Corrupção, recuperação de gravação, armazenamento e isolamento de perfil | PASS nos contratos obrigatórios |
| Catálogo/importação e atualização/recuperação desktop sintéticos | 2 PASS; 90,53 s |
| Crash → relançar → dashboard/jogo/retorno → fechar → relançar, HDD preservado | PASS sintético |
| Controles negativos do gate | 23 PASS; 20,724 s |
| Ciclo original/input/áudio/perfis; importação UI; save/load; Windows limpo | 4 NOT RUN |
| Runner e reverificação independente | Saída 1 esperada; apenas os quatro NOT RUN |

São 40 resultados obrigatórios PASS e quatro NOT RUN no recibo. Nenhum FAIL,
SKIP ou KNOWN FAILURE foi convertido em PASS ou omitido para certificar.

## Execução reproduzível

```powershell
& .local/toolchains/python-3.14.8/python.exe scripts/validation/stability_gate.py run --source C:/Users/fabeg/Desktop/x360 --build C:/Users/fabeg/Desktop/x360/.local/build/win-amd64 --desktop C:/Users/fabeg/Desktop/x360/.local/build/desktop-only --output C:/Users/fabeg/Desktop/x360/.local/evidence/stable-02/run-01
& .local/toolchains/python-3.14.8/python.exe scripts/validation/stability_gate.py verify .local/evidence/stable-02/run-01/report.json
```

Sem `--reuse-contracts` ou `--reuse-candidate`: nova execução dos 37 contratos,
desktop e recuperação. Os builds Release passaram pelos targets do runner via
`devshell.cmd`; não se executou a suíte completa nem conteúdo original.
O alias global `python` aponta para o Microsoft Store e não executa Python;
usou-se o interpretador 3.14.8 já pinado, sem instalação ou alteração de PATH.
Logs, XML, EXE/DLL e hashes estão no recibo local. O verificador exige HEAD
igual ao SHA executado: avançar a main com este relatório não recertifica o
novo commit nem autoriza reescrever a identidade do recibo.

## Revisão do ciclo real e limites da evidência

`src/supervisor/main.cpp::Session` mantém janela da plataforma e snapshot de
renderer/configuração durante dashboard → jogo → dashboard. My Games deve
emitir pedido Image válido, com flags aceitas, caminho e SHA do XEX registrado.
Cada transição reabre o perfil e autentica o produto/recibo; a versão 17559
fornece exclusivamente seu host, módulos e recursos. Dependência ausente ou
de outra versão impede lançamento; não há novo fallback.

`SessionPlatformArguments` fornece o profile root e content root da dashboard
ao jogo: mesmo perfil/HDD, cache do participante separado. O override de storage
segue os handoffs sem mudar o storage global. Perfil, corrupção, falhas de
gravação e recuperação têm contratos focados; progresso comercial ainda exige
o [ensaio real](persistence-rehearsal.md).

O retorno Guide/Xbox Home deve produzir handoff Dashboard e flags suportadas;
fechar a janela encerra a plataforma e não vale como retorno. Crash/timeout
não disparam retry automático; o relançamento é explícito. `Run` reserva pasta
nova atomicamente, recusa handoff antigo e só passa o handoff da transição
anterior da sessão atual. `RunProcess` espera o filho; handles são locais e
liberados por RAII, com Job Object kill-on-close para cancelamento/timeout.
Os processos novos não recebem ponteiros do processo encerrado.

Áudio/input usam o runtime e a janela de cada novo participante. O histórico
Forza 1 registrou drenagem de oito frames de áudio, callbacks quiescentes,
entrada e retorno na mesma janela, mas em outro lock. Isso orienta a inspeção;
não demonstra ausência de áudio duplicado, foco perdido, recursos GPU/áudio
pendentes ou input quebrado no ciclo original desta revisão. Esse aceite
continua NOT RUN. Nenhum novo defeito genérico foi demonstrado nesta inspeção;
não se acrescentaram testes espelhando implementação, patches SDK, ajustes de
renderer ou condições por título. Não há alegação de ganho de performance.

## Preparação do próximo ensaio

Recomendação: **Forza Horizon 1 integral em disco**, pela evidência histórica
mais forte de ciclo e escrita de progresso. Exige licença/conteúdo autorizados,
recibo renovado e autorização específica de modo/duração antes de gameplay.
Forza 2 tem recibo atual, mas avanço recente bloqueado em “Pressione Start”;
Minecraft/Castle em trial não certificam save/load integral.

Foram preparadas `seed` e `working` independentes do console completo Retail
17559 e cache do Forza 1 em `.local/evidence/stable-02/persistence-01/`.
Inventário privado confere 3.095 arquivos, 9.097.881.575 bytes por cópia;
igualdade SHA-256 de ambas e preservação da origem confirmadas em 137,422 s.
Inclui biblioteca instalada e todos os perfis/saves, sem links ou filtragem do
HDD. Nada foi iniciado. [Licença, passos exatos, limites e bloqueios de UI e
Windows limpo](persistence-rehearsal.md) ficam preparados para outra autorização.

Conteúdo original e dados de origem preservados; mudanças versionadas somente
documentais. XERJ e inventários privados não entram no Git. Sem Computer Use,
sessão visual, gameplay, push, tag ou Release. A estabilidade v1.0.0 permanece
recusada enquanto qualquer um dos quatro critérios manuais estiver pendente.
