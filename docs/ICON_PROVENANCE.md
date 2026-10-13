# Procedência do ícone

O ícone atual é geometria original criada para x360-native em 2026-10-13.
O gerador `scripts/generate-desktop-icon.ps1` contém toda a fonte: dois glifos
pixelados XN desenhados em uma grade, fundo petróleo e primeiro plano claro.
Não usa fonte tipográfica, imagem, logotipo, modelo generativo ou recurso externo.
Conserva o monograma e a família de cores da interface; não representa o símbolo
Xbox nem afiliação com a Microsoft. O identificador do recurso Windows permanece
inalterado. Titular público confirmado pelo responsável: **x360-native**.
Licença: BSD-3-Clause, texto integral no `LICENSE` da raiz.

Gerar: `powershell -NoProfile -File scripts/generate-desktop-icon.ps1`.
Verificar sem modificar: acrescentar `-Check`. O ICO contém imagens DIB 32 bits
de 16, 32, 48 e 64 pixels. Não depende de fontes instaladas ou relógio da máquina.
O inventário existente registra o hash do ICO; o gate confere os bytes e o gerador.

O recurso anterior foi introduzido em
`fba55736988b3336fb7abc0bf069be475c2eb055`, blob
`b56e2d47a04a7c916b2b5aa95abeb29f576ac83f`, SHA-256
`31ca993c85b0896369797c1b270de184b1b6c6250a78b4c4fb65b4e9658648bd`.
O histórico disponível não documenta sua criação nem uma licença específica.
Ele foi substituído, sem afirmar autoria retroativa. Não integra o novo ZIP ou
seu arquivo de fontes. O histórico Git não foi reescrito: publicar o histórico
antigo ainda exige decidir como tratar esse recurso sem proveniência comprovada.
