import re
import argparse
import os

def clean_text(text):
    """
    Очищает текст от ссылок, пунктуации, чисел и других нежелательных символов.
    """
    # Удаление URL-ссылок
    text = re.sub(r'http\S+|www\.\S+', '', text)
    
    # Удаление цифр
    text = re.sub(r'\d+', '', text)
    
    # Удаление пунктуации и специальных символов
    # Оставляем только буквы (русские и английские) и пробелы
    text = re.sub(r'[^а-яА-Яa-zA-Z\s]', '', text)
    
    # Нормализация пробелов (удаление лишних пробелов)
    text = re.sub(r'\s+', ' ', text)
    
    # Приведение к нижнему регистру
    text = text.lower()
    
    return text.strip()

def process_file(input_path, output_path):
    """
    Читает исходный файл, очищает текст и записывает в выходной файл.
    """
    try:
        with open(input_path, 'r', encoding='utf-8') as infile:
            text = infile.read()
    except FileNotFoundError:
        print(f"Ошибка: Файл '{input_path}' не найден.")
        return
    except Exception as e:
        print(f"Ошибка при чтении файла: {e}")
        return

    cleaned_text = clean_text(text)

    try:
        with open(output_path, 'w', encoding='utf-8') as outfile:
            outfile.write(cleaned_text)
        print(f"Очищенный текст сохранен в '{output_path}'.")
    except Exception as e:
        print(f"Ошибка при записи файла: {e}")

def generate_output_filename(input_path):
    """
    Генерирует имя выходного файла, добавляя суффикс '_cleaned' перед расширением.
    """
    base, ext = os.path.splitext(input_path)
    return f"{base}_cleaned{ext}"

def main():
    parser = argparse.ArgumentParser(description="Очистка текстового файла от нежелательных символов.")
    parser.add_argument('input_file', help='Путь к исходному текстовому файлу.')
    parser.add_argument('output_file', nargs='?', help='Путь к выходному файлу. Если не указан, будет создан файл с суффиксом _cleaned.')
    
    args = parser.parse_args()
    
    input_path = args.input_file
    output_path = args.output_file if args.output_file else generate_output_filename(input_path)
    
    process_file(input_path, output_path)

if __name__ == "__main__":
    main()
