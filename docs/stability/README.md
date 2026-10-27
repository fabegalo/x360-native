# STABLE-01 — contrato candidato, baseline Preview 25

Primeiro alvo: **Windows x64, Release, Xenos D3D12, dashboard Retail
17559 original**. Ainda não certificado. Native e Vulkan permanecem
experimentais. Nenhuma tag ou publicação faz parte desta etapa.

Obrigatório para estabilidade: importação e preparação pelo aplicativo;
Home/My Games → jogo → Guide/Home → retorno à mesma dashboard; perfis,
entrada e áudio; progresso salvo e carregado depois de reiniciar jogo e
plataforma; proteção de dados e atualização recuperável. Cada aceite pertence
ao conteúdo/versão/recibo/backend/commit/lock testados. Não vale transferir
aceite entre backends, dashboards ou revisões de jogos.

## Gate reproduzível

`criteria.json` fixa os nomes individuais obrigatórios. O runner executa apenas
esse conjunto focado, dois testes desktop existentes e o cenário novo de
recuperação. Não abre originais nem usa Computer Use. Usa os binários Release
atuais da main e compila os testes desktop alterados na worktree isolada.
Não muda o SDK: verifica o pin e os hashes de todos os patches/arquivos finais.

Configurar o desktop isolado com `scripts/devshell.cmd cmake`, usando
`-DX360_DESKTOP_ONLY=ON`, `-DCMAKE_BUILD_TYPE=Release`,
`-DCMAKE_C_FLAGS=-march=x86-64-v2`, `-DCMAKE_CXX_FLAGS=-march=x86-64-v2`,
compiladores LLVM 22.1.8 e `-DX360_REXGLUE_DIR=<checkout-main>/external/rexglue-sdk`.
Compartilhar fontes genéricas verificadas do SDK não compartilha originais.

Na worktree, com Python 3.14.8 pinado (substituir caminhos absolutos):

```powershell
python scripts/validation/stability_gate.py run --source C:/path/main --build C:/path/main/.local/build/win-amd64 --desktop C:/path/worktree/.local/build/stable-desktop --output C:/path/worktree/.local/evidence/stable-01/run-01
python scripts/validation/stability_gate.py verify C:/path/worktree/.local/evidence/stable-01/run-01/report.json
```

Saída 0 exige todos os resultados obrigatórios PASS e evidências presentes e
íntegras. Saída 1 preserva FAIL, KNOWN FAILURE, SKIP e NOT RUN como bloqueios.
Sem resultados, build falho, conjunto JUnit vazio/incompleto/duplicado, hash
divergente ou artefato omitido: reprovação. Cada execução exige diretório novo.
Comandos, limites, logs, XML, binários/DLLs e hashes ficam no recibo local.
O verificador relê o XML; editar apenas o estado JSON não transforma falha em PASS.
Hashes verificam integridade, não autenticidade de uma observação humana.
`--reuse-contracts <report.json>` permite aproveitar os contratos da main sem
repeti-los após corrigir apenas testes desktop/gate. Exige mesma identidade,
mesmo conjunto de artefatos main/SDK e evidência intacta; preserva inclusive FAIL
e SKIP. O restante do gate continua executando. Não recertifica um binário novo
com resultados de outro. Timeouts encerram a árvore dos processos no Windows.
`--reuse-candidate <report.json>` aplica a mesma regra ao desktop e cenário
sintético: todos os artefatos nativos, SDK, lock e política precisam ser idênticos.
Permite fortalecer apenas o verificador sem repetir regressões já aprovadas.

Os quatro aceites reais começam **NOT RUN** em toda execução automatizada;
o runner não os promove. `verify` aceita um recibo revisado com evidência
humana por critério, identidade completa igual à execução, `content` com
arquivos locais e SHA-256, e `observation` descrevendo procedimento/resultado.
Isso exige revisão humana dos critérios abaixo, nunca apenas preencher PASS.
Não commitar recibos com conteúdo, perfis, saves ou XERJ. XERJ exclusivamente local.

## Evidência real obrigatória e bloqueios P0

| ID do critério | Evidência necessária / bloqueio atual |
|---|---|
| original-17559-cycle-input-audio-profiles | Recibos atuais da 17559 e jogo, origem/SHA-256 de todos os módulos/fontes da versão; vídeo/observação e logs de Home, My Games, perfis, entrada, áudio, Guide e retorno. Evidência histórica parcial não recertifica a main. |
| application-import-prepare | Importar/preparar pelo aplicativo com recibo íntegro e inscrição na My Games; interromper preparação e comprovar que produto incompleto não executa, com recuperação explícita. Contratos automáticos não aprovam a UI. |
| commercial-save-load | Jogo suportado e licença/modo que realmente permite salvar; progresso identificável, persistência real, relançamento e carregamento pela UI original. Nenhum jogo tem este aceite hoje. |
| clean-windows-distribution-update | Pacote Release em Windows x64 limpo; primeira execução, dependências ausentes, preparo interrompido, atualização/rollback e preservação de dados. Ainda sem aceite. |

Save/load real requer autorização específica para título, modo e duração;
originais e licença fornecidos localmente; preparo atual, XUID/Title ID/revisão
identificados; cópia isolada do **console completo/HDD único**, inventário e
hashes antes/depois. Registrar um progresso observável, salvar pela função
original, retornar, fechar normalmente, relançar e confirmar exatamente o
progresso. Repetir após reiniciar a plataforma; demonstrar isolamento de outro
perfil/título e preservação da origem. Trial Minecraft não comprova save/load
da versão integral. Não fabricar saves nem alterar licença/oráculos.
Esta tarefa não executou conteúdo original: autorizações anteriores de sessões
da auditoria não são uma nova autorização de ensaio visual/save/load.

Windows limpo exige VM/máquina sem toolchain/dev DLLs no PATH; registrar versão
do Windows, CPU/GPU/driver, pacote/inventário/SHA-256 e logs. Exercitar ausência
de VC runtime/Windows SDK/MSVC/rede, download inválido, disco cheio/sem acesso,
interrupção de extração/codegen/build/registro, dependência original ausente ou
de outra dashboard, arquivo bloqueado e morte em cada fronteira de atualização.
Exigir erro claro, ausência de estado pronto falso, retomada/rollback e hashes
dos perfis/saves/conteúdo preservados. Probes sintéticos não substituem esse ensaio.

## Limites e sequência das próximas correções

1. Integrar e acompanhar a correção de colisão de diretórios de execução:
   main mantém FAIL no relançamento; a candidata aloca diretório exclusivo
   atomicamente, preservando a recusa de handoff antigo e todos os logs.
2. Renovar somente os recibos necessários ao jogo escolhido para save/load;
   resolver falhas de preparo sem afrouxar verificações. Castle tem marcador
   incompleto; outros títulos têm recibos antigos conforme auditoria Preview 25.
3. Escolher conteúdo/licença e obter autorização do ensaio real acima; corrigir
   o primeiro bloqueio reproduzível até completar save/load e ciclo original.
4. Completar entrada/Guide/perfis/áudio e fidelidade Xenos na mesma identidade;
   classificar/resolver diferenças gráficas aplicáveis, sem remover efeitos.
5. Executar distribuição limpa e recuperação com o pacote exato; só então
   revisar o contrato estável. Publicação requer outra tarefa.

As falhas históricas Native MEMEXPORT/oclusão e as 45 diferenças Xenos R8/API
continuam registradas na [auditoria Preview 25](../reports/release-audit-2026-10-27.md).
O teste upstream Depfile permanece falha histórica, fora deste conjunto focado.
Não foram reexecutados GPU, jogos ou a suíte completa por mudanças em testes/gate.
Nenhuma correção de renderer/SDK ou alegação de desempenho nesta etapa.

Confronto: [STATUS](../../STATUS.md), [compatibilidade](../compatibility.md),
[checklist §§5.3, 6, 13, 16–18](../CHECKLIST_FUNCIONAL_x360-native.md) e auditoria
concordam nos bloqueios acima. Compilar/importar não prova gameplay;
C7/C8 provam contratos e persistência sintética, não progresso comercial.
