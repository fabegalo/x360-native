# Forza Horizon — colisões e `vmsum3fp128`/`vmsum4fp128` (P0, 2026-10-22)

Alvo: Forza Horizon `4D5309C9`, `default.xex` 0.0.0.10, SHA-256
`db40df605ade49a612b35a7a24c38f6004bcb17a88ed6b48288de16df9e3987c`
(`titles/forza_horizon/preparation.toml`). Defeito relatado pelo mantenedor:
o jogador atravessa outros veículos.

**Estado: aberto.** A correção genérica do codegen está implementada e testada
em PPC; o AOT do Forza foi regenerado (ver §4). Nenhuma execução do jogo foi
feita nesta etapa (exige autorização); colisões reais **não** foram
confirmadas antes nem depois.

## 1. Reprodução (F1)

Não executada: sem sessão de gameplay autorizada. Os quatro cenários
(trânsito, adversários, obstáculos estáticos, carros de replay/Rivals) ficam
pendentes, com a mesma versão e os mesmos dados antes/depois. Para o "antes",
os produtos pré-0141 foram guardados em
`.local/build/diagnostics/forza-pre-0141/` (`forza_horizon.exe` SHA-256
`88de6d86736d2010b25ebb3383b11ff116d6bac29426dce9941c5a0cb1e8895e`).

## 2. Defeito do codegen (F2/F3) — fato

`build_vmsum3fp128/4fp128` emitiam `simde_mm_dp_ps`. Para entradas finitas
cujo produto escalar transborda float32, o resultado era ±inf (e `0xFFC00000`
quando produtos opostos transbordam), em vez do QNaN `0x7FC00000` do Xenon
(referência: Xenia `DOT_PRODUCT_3/4` + `EmitDotProductResult`;
[rexglue-sdk#491](https://github.com/rexglue/rexglue-sdk/issues/491)).
Correção genérica: patch `0141-vmsum-overflow-qnan.patch`, descrito em
`patches/rexglue-sdk/README.md`, com o teste `sdkcheck.Vmsum` (41 falhas
registradas antes, 160 416 verificações depois) e a suíte `ppc.*` do SDK
(1459 testes) passando. Custo medido no caminho finito: +0,4 a +0,65 ns por
instrução em microbenchmark; impacto por quadro não medido.

## 3. Rotina GJK no XEX 0.0.0.10 (F4) — análise estática

O ReImagined documenta o `default.xex` **0.0.0.7** (disco Europa/Ásia, Media
ID `2B7A1346`), sem SHA-256 do arquivo. Os endereços foram conferidos no
0.0.0.10 local com `x360n-content disasm/callers/xref`:

| Endereço | Encontrado | Situação |
|---|---|---|
| `0x82D43290` | função `.pdata` 0x82D43290–0x82D4342C; chama `0x82D420F0` em `0x82D4333C` antes do primeiro ponto de suporte e, se retornar verdadeiro, pula ao fim (`0x82D433AC`) | fato |
| `0x82D420F0` | função `.pdata` 0x82D420F0–0x82D42198 (teste de término) | fato |
| `0x82D42120` | `vmsum3fp128 v61,v63,v63` (\|v\|² de `[[r3+156]+176]`), gravado em `r3+128` e comparado com a distância anterior | fato |
| `0x82D41608` | função `.pdata` 0x82D41608–0x82D416B8 | fato; papel (pontos com simplex vazio) não verificado |
| `-FLT_MAX` inicial em `v` | o vetor é preenchido em tempo de execução; `0x834AA1E0` recebe `{0, 1, 0, 0}` (direção inicial), não `-FLT_MAX` | hipótese do ReImagined, não verificada aqui |

O sintoma descrito pelo ReImagined no 0.0.0.7 é **crash** ao carregar o free
roam (vértices do simplex nunca escritos), não carros atravessáveis.
Hipótese: o mesmo término prematuro do GJK, com memória não escrita diferente,
pode produzir "sem contato" em vez de falha. O `STATUS.md` já registrava uma
queda do Forza após 302.052 ms por leitura guest `0x02A2C6F8`, sem pilha;
compatível com esse mecanismo, mas sem evidência que os ligue. Só uma sessão de gameplay
(com diagnóstico opcional, desligado por padrão, observando entrada/saída de
`0x82D420F0` e `0x82D43290`) pode confirmar ou descartar.

## 4. AOT regenerado (F5)

`scripts/build-title.ps1 -Title forza_horizon` (Release), sem estado
incremental autenticado → reconstrução segura: ferramenta 104 s, codegen
102 s (produtor trocado → `--ignore-stamp`), build 250 s, todas com exit 0
(`.local/evidence/vmsum/forza-build.log`).

- Antes: 2738 sites `vmsum3fp128/4fp128` no código gerado, todos com `dpps`.
  Depois: os 2738 usam `rex::ppc::simde_mm_vmsum3fp128/4fp128`; nenhum
  `simde_mm_dp_ps` restante.
- `sub_82D420F0` (`forza_horizon_recomp.39.cpp`), site `0x82D42120`:
  `simde_mm_store_ps(ctx.v61.f32, rex::ppc::simde_mm_vmsum3fp128(...v63..., ...v63...))`.
- `forza_horizon.exe` novo: SHA-256
  `2255da781381910af3ab962c382b140f3eb71360f4ed9d401fa7ae0887c8f851`
  (anterior `88de6d86…8895e`, guardado). O recibo
  `forza_horizon.exe.preparation.json` registra `intrinsics.h`
  `258b51ce…7fde8` e `vector.cpp` `47e4eb7c…bdbd8`, os hashes do lock após 0141.
- O XEX original não foi alterado (SHA-256 conferido igual ao da preparação).
- Fato: "compilou". Não verificado: inicia, carrega free roam, colide.

## 5. Hipóteses secundárias (F6)

Não investigadas: só se a evidência de gameplay indicar necessidade, em
patches separados — `XFileSectorInformation` (o SDK devolve hash do caminho),
preenchimento `0xBE` das pilhas guest, espera fixa de 100 ms em conclusões
OVERLAPPED.

## 6. Renderers (F8)

A mudança está só no código PPC gerado e no header de intrínsecos; não toca
shaders, EDRAM, resolução ou taxa. Xenos/D3D12, Xenos/Vulkan, Native/D3D12 e
Native/Vulkan: sem teste funcional nesta etapa.

## Pendências

- Sessão de gameplay autorizada: cenários de F1 antes (build guardado) e
  depois, mesma save descartável, velocidades e direções diferentes.
- Diagnóstico GJK opcional, se a colisão continuar ausente.
- Hardware: precisão de resultados finitos (float32 `dpps` vs float64 do Xenia).
- Upstream: a correção equivalente ainda não existe no ReXGlue (#491 aberto).
