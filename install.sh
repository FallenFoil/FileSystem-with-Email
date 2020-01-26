CMDS="apt"

for i in $CMDS
do
	command -v $i >/dev/null && continue || { echo "$i command not found. Please install these packages: libcurl4-gnutls-dev and uuid-dev."; exit 1; }
done

sudo apt install libcurl4-gnutls-dev
sudo apt-get install uuid-dev