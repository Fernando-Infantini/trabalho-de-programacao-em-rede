import struct

# Tamanho de cada imagem de teste.
LARGURA = 1920
ALTURA = 1080
QUANTIDADE = 5


def gerar_bmp(nome, numero):
    # Cada pixel usa 3 bytes: azul, verde e vermelho.
    bytes_por_pixel = 3
    tamanho_linha = LARGURA * bytes_por_pixel

    # O formato BMP precisa que a linha seja múltipla de 4 bytes.
    padding = (4 - (tamanho_linha % 4)) % 4
    tamanho_linha_completa = tamanho_linha + padding
    tamanho_imagem = tamanho_linha_completa * ALTURA

    # O cabeçalho BMP usado aqui possui 54 bytes.
    tamanho_arquivo = 54 + tamanho_imagem

    with open(nome, "wb") as arquivo:
        # Cabeçalho principal do arquivo BMP.
        arquivo.write(b"BM")
        arquivo.write(struct.pack("<I", tamanho_arquivo))
        arquivo.write(struct.pack("<HH", 0, 0))
        arquivo.write(struct.pack("<I", 54))

        # Informações básicas da imagem.
        arquivo.write(struct.pack("<I", 40))
        arquivo.write(struct.pack("<i", LARGURA))
        arquivo.write(struct.pack("<i", ALTURA))
        arquivo.write(struct.pack("<H", 1))
        arquivo.write(struct.pack("<H", 24))
        arquivo.write(struct.pack("<I", 0))
        arquivo.write(struct.pack("<I", tamanho_imagem))
        arquivo.write(struct.pack("<i", 2835))
        arquivo.write(struct.pack("<i", 2835))
        arquivo.write(struct.pack("<I", 0))
        arquivo.write(struct.pack("<I", 0))

        # Cada arquivo recebe uma cor diferente para facilitar o teste.
        azul = (40 * numero) % 256
        verde = (80 * numero) % 256
        vermelho = (120 * numero) % 256
        pixel = bytes([azul, verde, vermelho])

        # Monta uma linha e repete até completar a imagem.
        linha = pixel * LARGURA
        linha += b"\x00" * padding

        for _ in range(ALTURA):
            arquivo.write(linha)


for i in range(1, QUANTIDADE + 1):
    nome = f"imagem{i}.bmp"
    gerar_bmp(nome, i)
    print(f"{nome} criada")
