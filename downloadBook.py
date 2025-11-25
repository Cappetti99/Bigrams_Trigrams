import urllib.request
import urllib.error
import os
import time
from pathlib import Path

def scarica_libro_gutenberg(book_id, cartella_output="libri_gutenberg"):
    """
    Scarica un libro da Project Gutenberg dato il suo ID
    
    Args:
        book_id: ID numerico del libro su Gutenberg
        cartella_output: nome della cartella dove salvare i libri
    
    Returns:
        True se il download ha successo, False altrimenti
    """
    # Crea la cartella se non esiste
    Path(cartella_output).mkdir(exist_ok=True)
    
    # Controlla se il file esiste già
    nome_file = f"{cartella_output}/libro_{book_id}.txt"
    if os.path.exists(nome_file):
        print(f"⏭️  Libro {book_id} già presente, skip")
        return True
    
    # Lista di possibili URL da provare
    url_variants = [
        f"https://www.gutenberg.org/files/{book_id}/{book_id}-0.txt",
        f"https://www.gutenberg.org/files/{book_id}/{book_id}-8.txt",
        f"https://www.gutenberg.org/files/{book_id}/{book_id}.txt"
    ]
    
    print(f"Scaricamento libro {book_id}...")
    
    for url in url_variants:
        try:
            with urllib.request.urlopen(url, timeout=30) as response:
                contenuto = response.read().decode('utf-8')
                
                # Salva il file
                with open(nome_file, 'w', encoding='utf-8') as f:
                    f.write(contenuto)
                
                print(f"✓ Libro {book_id} salvato in {nome_file}")
                return True
                
        except urllib.error.HTTPError as e:
            if e.code == 404:
                continue  # Prova il prossimo URL
            else:
                print(f"✗ Errore HTTP {e.code} per libro {book_id}")
                return False
        except Exception as e:
            print(f"✗ Errore durante il download del libro {book_id}: {str(e)}")
            return False
    
    print(f"✗ Impossibile scaricare il libro {book_id} (nessun formato disponibile)")
    return False


def scarica_libri(lista_id, cartella_output="book_gutenberg", pausa=1):
    """
    Scarica una lista di libri da Project Gutenberg
    
    Args:
        lista_id: lista di ID numerici dei libri
        cartella_output: cartella dove salvare i libri
        pausa: secondi di pausa tra un download e l'altro (per non sovraccaricare il server)
    """
    print(f"\n=== Inizio download di {len(lista_id)} libri ===\n")
    
    successi = 0
    fallimenti = 0
    skipped = 0
    
    for book_id in lista_id:
        # Controlla se esiste già
        if os.path.exists(f"{cartella_output}/libro_{book_id}.txt"):
            skipped += 1
            print(f"⏭️  Libro {book_id} già presente, skip")
            continue
            
        if scarica_libro_gutenberg(book_id, cartella_output):
            successi += 1
        else:
            fallimenti += 1
        
        # Pausa tra i download per non sovraccaricare il server
        if book_id != lista_id[-1]:  # Non aspetta dopo l'ultimo
            time.sleep(pausa)
    
    print(f"\n=== Download completato ===")
    print(f"Nuovi download: {successi}")
    print(f"Già presenti (skipped): {skipped}")
    print(f"Fallimenti: {fallimenti}")
    print(f"Totale libri disponibili: {successi + skipped}")
    print(f"Libri salvati in: {cartella_output}/")


if __name__ == "__main__":
    TARGET_NUM_LIBRI = 1500
    
    # 1. Partiamo con una lista di classici garantiti (circa 50) per avere testi lunghi e di qualità
    libri_da_scaricare = [
        1342, 84, 1661, 11, 2701, 1952, 174, 1260, 98, 46,      # Austen, Shelley, Doyle, Carroll, Melville...
        345, 74, 1232, 2600, 1400, 16, 2814, 768, 1497, 219,    # Dickens, Twain, Machiavelli...
        514, 120, 33, 35, 600, 1399, 1250, 1404, 215, 158,
        786, 43, 1998, 209, 829, 135, 2148, 203, 161, 2554,
        5200, 996, 19942, 2500, 2641, 394, 4300, 140, 3825, 61
    ]
    
    # 2. Riempiamo il resto automaticamente iterando sugli ID
    # Project Gutenberg inizia da 1. Saltiamo quelli già in lista.
    current_id = 1
    while len(libri_da_scaricare) < TARGET_NUM_LIBRI:
        if current_id not in libri_da_scaricare:
            libri_da_scaricare.append(current_id)
        current_id += 1
    
    # Ordiniamo per pulizia
    libri_da_scaricare.sort()
    
    print(f"📚 Preparata lista di {len(libri_da_scaricare)} libri")
    print(f"   - {50} classici selezionati")
    print(f"   - {TARGET_NUM_LIBRI - 50} generati sequenzialmente (ID 1-{current_id})")
    print(f"⏱️  Tempo stimato (se dovessero essere scaricati tutti): ~{len(libri_da_scaricare) * 1.0 / 60:.1f} minuti")
    print("   (I libri già presenti verranno saltati istantaneamente)\n")
    
    # Scarica i libri
    scarica_libri(libri_da_scaricare, cartella_output="book_gutenberg", pausa=1)
    
    print("\n💡 Per trovare l'ID di un libro: gutenberg.org/ebooks/[ID]")
    
    # Verifica unicità
    libri_originali = len(libri_da_scaricare)
    libri_unici = list(set(libri_da_scaricare))
    
    if libri_originali != len(libri_unici):
        print(f"⚠️  ATTENZIONE: trovati {libri_originali - len(libri_unici)} duplicati!")
        libri_da_scaricare = sorted(libri_unici)
    
    print(f"📚 Download di {len(libri_da_scaricare)} libri UNICI da Project Gutenberg")
    print(f"⏱️  Tempo stimato: ~{len(libri_da_scaricare) * 1.5 / 60:.1f} minuti\n")
    
    # Scarica i libri
    scarica_libri(libri_da_scaricare, cartella_output="book_gutenberg", pausa=1)
    
    print("\n💡 Per trovare l'ID di un libro: gutenberg.org/ebooks/[ID]")