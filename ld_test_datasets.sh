fetch_dataset () {
local name=$1
local url="https://raw.githubusercontent.com/saitoi/recuperacao-informacao/main/data/${name}.zip"

if [ -d "${name}" ]; then
echo "[ok] ${name} já consta em ${name}"
return
fi

echo "[..] Baixando ${name}.zip..."
curl -L -o "${name}.zip" "$url"

echo "[..] Extraindo para data/${name}/"
unzip "${name}.zip"

rm "${name}.zip"
echo "[ok] ${name} pronto!"
}

fetch_dataset dataset1
fetch_dataset dataset2
