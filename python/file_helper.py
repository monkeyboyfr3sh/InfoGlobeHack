def get_file_total_bytes( file_path ):
    total_bytes = 0
    with open(file_path, 'rb') as binary_file:
        total_bytes = len(binary_file.read())
    return total_bytes