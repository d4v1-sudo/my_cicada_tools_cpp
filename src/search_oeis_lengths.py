import csv
import sys
import os

def load_sequences(csv_path):
    sequences = {}
    with open(csv_path, 'r', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        for row in reader:
            page = row['Page']
            type_ = row['Type']
            length = row['Length']
            key = f"{page}_{type_}"
            if key not in sequences:
                sequences[key] = []
            sequences[key].append(length)
    return sequences

def search_oeis(sequences, oeis_path, output_path):
    # Prepare target sequence strings for fast matching
    # A sequence might be '4,5,2,7' -> we look for ',4,5,2,7,'
    target_strings = {}
    for key, seq in sequences.items():
        if len(seq) < 3:
            continue # Too short to be meaningful
        target_strings[key] = "," + ",".join(seq) + ","
    
    print(f"Loaded {len(target_strings)} length sequences with >=3 elements.")
    
    hits = []
    with open(oeis_path, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            if not line.startswith("A"):
                continue
            parts = line.strip().split(" ", 1)
            if len(parts) != 2:
                continue
            oeis_id = parts[0]
            # Add commas to the ends to ensure we match whole numbers
            data_str = parts[1]
            if not data_str.startswith(","): data_str = "," + data_str
            if not data_str.endswith(","): data_str = data_str + ","
            
            for key, target_str in target_strings.items():
                if target_str in data_str:
                    # target_str includes leading and trailing commas, e.g. ',4,5,2,'
                    matched_sequence = target_str.strip(',')
                    hits.append((key, oeis_id, matched_sequence))
                    print(f"Match found! {key} -> {oeis_id} (Sequence: {matched_sequence})")

    with open(output_path, 'w', encoding='utf-8') as f:
        for key, oeis_id, seq in hits:
            f.write(f"{key} found in {oeis_id} | Sequence: {seq}\n")
    print(f"Total hits: {len(hits)}")

if __name__ == "__main__":
    csv_path = "../output/structural_segments.csv"
    oeis_path = "../wordlists/oeis.txt"
    output_path = "../output/oeis_length_hits.txt"
    
    if not os.path.exists(csv_path):
        print(f"Error: {csv_path} not found.")
        sys.exit(1)
        
    if not os.path.exists(oeis_path):
        print(f"Error: {oeis_path} not found.")
        sys.exit(1)
        
    sequences = load_sequences(csv_path)
    search_oeis(sequences, oeis_path, output_path)
