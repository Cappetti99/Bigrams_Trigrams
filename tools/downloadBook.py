import urllib.request
import urllib.error
import os
import time
from pathlib import Path

def scarica_libro_gutenberg(book_id, cartella_output="libri_gutenberg"):
    """
    Download a book from Project Gutenberg given its ID
    
    Args:
        book_id: Numeric ID of the book on Gutenberg
        cartella_output: Name of the folder where books will be saved
    
    Returns:
        True if download succeeds, False otherwise
    """
    # Create folder if it doesn't exist
    Path(cartella_output).mkdir(exist_ok=True)
    
    # Check if file already exists
    nome_file = f"{cartella_output}/libro_{book_id}.txt"
    if os.path.exists(nome_file):
        print(f"⏭️  Book {book_id} already present, skipping")
        return True
    
    # List of possible mirrors and URLs to try (including alternative mirrors)
    url_variants = [
        # HTTP instead of HTTPS (some firewalls block HTTPS to Gutenberg)
        f"http://www.gutenberg.org/files/{book_id}/{book_id}-0.txt",
        f"http://www.gutenberg.org/files/{book_id}/{book_id}-8.txt",
        f"http://www.gutenberg.org/files/{book_id}/{book_id}.txt",
        # Alternative mirrors (aleph.gutenberg.org)
        f"http://aleph.gutenberg.org/{book_id}/{book_id}-0.txt",
        f"http://aleph.gutenberg.org/{book_id}/{book_id}-8.txt",
        f"http://aleph.gutenberg.org/{book_id}/{book_id}.txt",
    ]
    
    print(f"Downloading book {book_id}...")
    
    # Add headers to appear as a normal browser
    headers = {
        'User-Agent': 'Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36'
    }
    
    for url in url_variants:
        try:
            req = urllib.request.Request(url, headers=headers)
            with urllib.request.urlopen(req, timeout=30) as response:
                contenuto = response.read().decode('utf-8')
                
                # Save the file
                with open(nome_file, 'w', encoding='utf-8') as f:
                    f.write(contenuto)
                
                print(f"✓ Book {book_id} saved to {nome_file}")
                return True
                
        except urllib.error.HTTPError as e:
            if e.code == 404:
                continue  # Try next URL
            else:
                print(f"✗ HTTP error {e.code} for book {book_id}")
                return False
        except Exception as e:
            print(f"✗ Error downloading book {book_id}: {str(e)}")
            return False
    
    print(f"✗ Unable to download book {book_id} (no format available)")
    return False


def scarica_libri(lista_id, cartella_output="book_gutenberg", pausa=1):
    """
    Download a list of books from Project Gutenberg
    
    Args:
        lista_id: List of numeric book IDs
        cartella_output: Folder where books will be saved
        pausa: Seconds to pause between downloads (to avoid overloading the server)
    """
    print(f"\n=== Starting download of {len(lista_id)} books ===\n")
    
    successi = 0
    fallimenti = 0
    skipped = 0
    
    for book_id in lista_id:
        # Check if already exists
        if os.path.exists(f"{cartella_output}/libro_{book_id}.txt"):
            skipped += 1
            print(f"⏭️  Book {book_id} already present, skipping")
            continue
            
        if scarica_libro_gutenberg(book_id, cartella_output):
            successi += 1
        else:
            fallimenti += 1
        
        # Pause between downloads to avoid overloading the server
        if book_id != lista_id[-1]:  # Don't wait after the last one
            time.sleep(pausa)
    
    print(f"\n=== Download completed ===")
    print(f"New downloads: {successi}")
    print(f"Already present (skipped): {skipped}")
    print(f"Failures: {fallimenti}")
    print(f"Total books available: {successi + skipped}")
    print(f"Books saved in: {cartella_output}/")


if __name__ == "__main__":
    TARGET_NUM_LIBRI = 1500
    
    # 1. Start with a list of guaranteed classics (~50 books) to get long, high-quality texts
    libri_da_scaricare = [
        1342, 84, 1661, 11, 2701, 1952, 174, 1260, 98, 46,      # Austen, Shelley, Doyle, Carroll, Melville...
        345, 74, 1232, 2600, 1400, 16, 2814, 768, 1497, 219,    # Dickens, Twain, Machiavelli...
        514, 120, 33, 35, 600, 1399, 1250, 1404, 215, 158,
        786, 43, 1998, 209, 829, 135, 2148, 203, 161, 2554,
        5200, 996, 19942, 2500, 2641, 394, 4300, 140, 3825, 61
    ]
    
    # 2. Fill the rest automatically by iterating through IDs
    # Project Gutenberg starts from 1. Skip those already in the list.
    current_id = 1
    while len(libri_da_scaricare) < TARGET_NUM_LIBRI:
        if current_id not in libri_da_scaricare:
            libri_da_scaricare.append(current_id)
        current_id += 1
    
    # 3. Verify uniqueness (remove any duplicates)
    libri_originali = len(libri_da_scaricare)
    libri_da_scaricare = sorted(list(set(libri_da_scaricare)))
    
    if libri_originali != len(libri_da_scaricare):
        print(f"⚠️  Removed {libri_originali - len(libri_da_scaricare)} duplicates from the list")
    
    # Print information
    print(f"📚 Prepared list of {len(libri_da_scaricare)} UNIQUE books")
    print(f"   - {50} selected classics")
    print(f"   - {len(libri_da_scaricare) - 50} generated sequentially")
    print(f"⏱️  Estimated time (if all need to be downloaded): ~{len(libri_da_scaricare) * 1.0 / 60:.1f} minutes")
    print("   (Books already present will be skipped instantly)\n")
    
    # Download the books
    scarica_libri(libri_da_scaricare, cartella_output="book_gutenberg", pausa=1)
    
    print("\n💡 To find a specific book ID: gutenberg.org/ebooks/[ID]")