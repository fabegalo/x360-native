# Castle: investigação separada de 0x820E15E8

Investigação estática em 17/10/2026, separada da preparação multimódulo do
Charlie Murder. Nenhum scanner, hint de produção, endereço original ou renderer
foi alterado por esta investigação.

Input: Castle Crashers 584108B7 / 0.0.1.3, `default.xex` SHA-256
`825e9c8cb951d62ce954901488c5b783349c98cc5c03bd08ea0739df3af5d9ad`.
ReXGlue no pin `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, com os 87 patches
existentes, incluindo 0088. O probe atual reproduziu exit 1, 3.908 funções,
252.610 palavras cobertas e exatamente um erro:

```text
UnresolvedCall 0x820E15E8 from 0x820E0D04
```

A desmontagem da imagem carregada confirma o chamador:

```asm
820E0D00  addi r3,r3,-4
820E0D04  b    0x820E15E8
```

O destino está fora de `.pdata`, depois de outro adjustor:

```asm
820E15DC  .long 0
820E15E0  addi r3,r3,-4
820E15E4  b    0x820EAAA8
820E15E8  lis  r11,-32255
           # corpo original: formação de três endereços e stores no objeto
820E1610  blr
820E1614  .long 0
820E1618  mflr r12             # próxima função com .pdata
```

O grafo registra `0x820E15E0` como `gap_fill`, extensão provisória de 52 bytes,
mas somente o bloco de oito bytes do adjustor é descoberto. Não há entrada nem
bloco para `0x820E15E8`. `0x820EAAA8` também tem autoridade `gap_fill`.

Explicação sustentada pelo código: `phase_gapfill.cpp` captura `knownCallables`
antes de registrar as novas regiões. Sua separação em um `b` depende de o
destino já estar nesse conjunto. O novo thunk virtual do 0088 reconhece somente
`lwz; lwz; mtctr; bctr`, portanto não se aplica. A descoberta posterior encerra
o adjustor no desvio e não promove genericamente destinos de `b` como faz com
`bl`. A extensão provisória não prova que o corpo seguinte será descoberto.

O hint existente `0x820E15E8 = {}` continua sendo a solução explícita de produção,
com procedência documentada em `titles/castle_crashers/analysis/`. O switch manual
de 16 slots e o hint `0x82114E20` também permanecem intactos. O relatório anterior
já demonstrou o mesmo erro com apenas o switch manual, sem o hint de função.

Uma correção futura precisa testar, de forma independente, limites após
adjustors, destinos registrados no próprio GapFill, recursão, desvios locais e
intervalos protegidos por metadata/configuração. Separar todo `b` ou promover
seu destino indiscriminadamente não está justificado por este caso. Não se
introduziu essa heurística para concluir o trabalho de módulos dependentes.

O build de produção com esses hints originais passou posteriormente com os
88 patches (0089 não muda o scanner): codegen 4 s, build nativo 68 s, preparação
total 341 s e recibo novo. Isso aprova a compilação configurada; não transforma
o probe genérico acima em sucesso nem elimina sua limitação de descoberta.

Na regressão final, o título configurado iniciou pelo My Games original da
17559, apresentou o menu do trial e retornou pelo Guide → Início do Xbox → Sim,
na mesma janela. Run `20261018T021911Z-session-1-castle_crashers`: 59.153 ms,
exit 0, zero erros/critical/chamadas AOT ausentes, 178 linhas de stubs existentes
do SDK. Não houve gameplay de campanha nem prova de que o caminho específico
`0x820E0D04` tenha executado; seu problema de descoberta permanece demonstrado
estaticamente. [Sessão e limites](aot-dependencies-2026-10-17.md).

Evidência local nova: `.local/evidence/aot-dependencies-2026-10-17/`:
`castle-generic.toml`, `castle-generic.tsv`, `castle-probe.log`,
`castle-adjustor.txt`, `castle-callers.txt`. Desmontagem do destino e pares com
switch: `.local/evidence/aot-discovery/`, preservados conforme
[relatório anterior](aot-discovery-2026-10-17.md). O probe é diagnóstico de
análise; seu exit 1 é falha esperada reproduzida, não aprovação de runtime.

## Revisão sobre a main de 18/10/2026

Com o pin e os 91 patches atuais, o driver oficial preparou o perfil de produção
em 553,71 s (792 compilações de objetos, 11 PCH), com recibo novo. Análise usando
o arquivo de hints real: 3.910 funções, 252.627 palavras, zero erros; adjustor,
leaf e destinos adjacentes independentes, switch de 16 slots e entradas geradas
conferidos. A retirada isolada do hint `0x820E15E8`, mantendo os outros metadados,
reproduziu o mesmo UnresolvedCall: 3.908 funções, 252.610 palavras, um erro/exit 1.
Essa limitação sem metadata não foi promovida a sucesso nem corrigida mediante
uma heurística indiscriminada. Originais, hints e SDK preservados; nenhum jogo
executado. [Produtos, hashes, comandos e limites do fechamento](audit-closure-2026-10-18.md).
