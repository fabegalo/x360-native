# Auditoria da próxima Technical Preview — 27/10/2026

## Identidade e preservação

`git fetch origin` confirmou a main inicial
`b50671c6e5bc0392ebf744e68135bb6083ba18e3`. A última Release pública era
`v0.59.0-preview.24`, em `20ea1ef0a5e3aec3ba71e867c3ed2eed9d2377b7`.
O intervalo contém 58 commits e passa de 139 para 201 patches ativos;
ReXGlue continua em `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`.

Foram conferidas as sete worktrees existentes e os processos do host, sem
build ativo no início. O SDK já tinha seus patches/materialização aplicados;
`c72-profile.log`, `prints/`, dados originais e worktrees foram preservados.
Não houve reset, limpeza de dados do usuário ou alteração de submódulos/pins.
Os 271 registros modificados no SDK correspondem à cadeia declarada e à
materialização libmspack conhecida, sem arquivo inesperado.

## Gates executados

| Gate | Resultado |
|---|---|
| Replay limpo pelo `apply-sdk-patches.ps1`, seguido de `-Check` | 201 patches/271 arquivos; hashes dos patches e arquivos finais aprovados; zero divergências com o SDK aplicado |
| Release raiz D3D12/Vulkan | Passou após corrigir include RenderDoc do probe Native Vulkan |
| AOT: GapFill, dados, materialização, CRT/fibers, MD, CTR | 767 verificações/19 casos; inclui destinos inválidos, tabelas inline, `.pdata`, corpos existentes e reconhecimento incompleto |
| Exports, perfis, storage, saves, input e conquistas relacionados | 3.033 asserções/61 casos; um caso de processos independentes skipped no lote, executado separadamente com sucesso |
| XAM/UI com as oito tags solicitadas | 903 verificações/28 casos; mesma contagem da entrega anterior |
| CTR nativo e callback após domínio limitado | 3.072 + 18 chamadas geradas aprovadas, incluindo LR/CTR e rejeições preservadas |
| Fibers nativas | Quatro trocas, continuations, GPR/FPR/VMX, stack e destruição aprovadas |
| Isolamento/supervisor/hotplug/PPCContext | 35 testes focados aprovados após completar dois fixtures de catálogo |
| Gráficos CTest | 35 aprovados; quatro falhas históricas Native preservadas; ABI corrigida e reexecutada separadamente |
| Xenos GPU | 2.324 casos/API; fixed16 sem falhas; 45 diferenças R8/API mantidas e classificadas |
| MSAA e rasterização/EDRAM GPU | D3D12/Vulkan aprovados, incluindo posições de amostra, offsets e wrapping |
| DXBC/SPIR-V | Regeneração temporária e `--verify` das três famílias aprovadas; SPIR-V validado; layers Khronos ausentes |
| Native Vulkan pelo plugin real | 48 frames sintéticos/4.128.768 canais, encerramento limpo; não é benchmark de FPS |
| Writebacks Native 1×/2×/4× | Fila bloqueada, slots em voo, leitura distinta, gerações CPU, sobreposição, eventos, staging de 17 MiB e resolves coloridos aprovados |
| Desktop catálogo/update recovery | Ambos aprovados; 29,60/92,86 s |
| Ferramentas de Release | 18 testes Python, compute-version, configuração Release e seleção de startup aprovados |

As únicas correções de código/testes desta auditoria são `98ca53e` e `7a7185e`.
O build inicialmente falhou por `renderdoc_app.h` ausente no include do target
`x360n_native_vulkan_probe`; nenhum teste usou esse build falho. O teste de ABI
ainda exigia recusar Vulkan mesmo após sua implementação. Agora exige a API
habilitada correta e recusa API inválida, ABI errada e create-info curto/nulo,
sem fallback e sem carregar Xenos. Os dois fixtures de sessão omitiam campos
obrigatórios de catálogo; receberam dados válidos, sem alterar as expectativas
positivas ou os controles de arquivos/versões inválidos.

## Falhas preservadas e performance

MEMEXPORT D3D12 mantém duas falhas (triângulo VS incompleto e alias residente);
Vulkan mantém essas duas e quatro conversões inteiras. Oclusão em ambas APIs
para na invalidação guest após publicação ZPD. A execução dos quatro probes
no baseline retido e no build atual produziu saídas completas e exit codes
idênticos. Não são sucessos nem correções desta auditoria.

N-PERF-01: três pares alternados/4.096 operações por cenário, RTX 4070,
driver 617.42, Release, sem builds/testes GPU concorrentes. Renderer e probe
têm os mesmos fontes do incremento integrado, exceto o CMake corrigido.
Variação pareada do lote: export 1× +2,18%; resolve 1× −7,80%; sync +0,36%;
none −35,54% (alta dispersão); resolve 2× −0,71%; 4× −0,18%. Instrumentação
OFF: export −0,93%, resolve −1,23%. Todos os bytes comparados são iguais.
Não apareceu regressão grave reproduzível; a amostra não estabelece ganho novo
nem FPS de gameplay. Os sete pares históricos completos continuam em
[N-PERF-01](../../tests/native/PERF-01.md).

## Conteúdo local e limites

O verificador de produção (`ReadCatalog`/`Inspect`/`CheckPreparation`) aceitou
os produtos Minecraft e Forza Horizon 2 com o lock 0214. Uma conferência
preliminar de reutilização incremental recusou seu inventário porque inclui
o executável auxiliar de preflight; não representa falha do recibo de execução.
17559 concluiu novo preparo oficial `fc9013998c854c0b98637bbf7869de89`, com
retorno zero em todas as etapas e remoção oficial do marcador incompleto.

1888, Forza Horizon 1 e Motocross têm originais identificados, mas recibos
antigos: foram recusados por fontes/lock diferentes. Castle já tinha marcador
incompleto no início. O recibo antigo do Charlie também diverge do SDK atual;
sua entrada não consta do catálogo selecionado. Não foram executados esses
jogos nem restaurados registros/arquivos removidos. **NOT RUN**: nova sessão
desses títulos, intro/Home/My Games/Guide por Computer Use fora dos dois jogos
autorizados, navegação entre botões da mensagem, save/reload comercial,
campanha, sessões longas, Windows limpo e validation layers ausentes.

As tentativas autorizadas usam novas cópias dos consoles de teste anteriores,
com os originais e o HDD/perfis/saves de origem preservados. Resultados do
harness limitado e observações Computer Use são registrados separadamente;
timeout controlado não é crash nem aceite de estabilidade.

| Tentativa nova | Observação Computer Use / API | Resultado do supervisor |
|---|---|---|
| `release-audit-minecraft-1` | Abertura, menu e tutorial 3D; A respondeu nos menus. Toques posteriores não demonstraram movimento no tutorial | 180.181 ms, limite, sem crash; 37/37 arquivos de origem preservados |
| `release-audit-message-a` | HUD/XUI original 17559; A confirmou o botão 1: result/extended 0, user 0; retorno visual ao Minecraft | 90.186 ms, limite, sem crash; 37/37 preservados |
| `release-audit-fh2-message-b` | HUD original; B concluiu 1627/1223, selected FFFFFFFF, user 0; abertura/“Pressione Start” preservadas | 180.291 ms, limite, sem crash; 37/37 preservados |

Minecraft manteve o executável `7769349e…ede32`; Forza `68f82b66…ade0d`,
iguais aos produtos 0214 anteriores. Em Forza, Return/X não avançaram de
“Pressione Start”, como já registrado nas tentativas anteriores sem controle
físico. **NOT RUN nesta auditoria: nova condução/HUD de gameplay Forza e
movimentação/extração Minecraft após o convite do tutorial.** Não foi alterado
input, renderer, licença, título ou contrato XAM para avançar. O gameplay
histórico 0212 permanece como evidência datada, sem recertificação implícita.

Evidências locais desta auditoria: `.local/evidence/release-audit-2026-10-27/`;
sessões em `.local/evidence/generic-games-2026-10-26/release-audit-*`.
Nenhum original, build ou ferramenta local integra os commits ou a distribuição.
Notas e limites da candidata: [0.69.27-preview.25](../releases/0.69.27-preview.25.md).
