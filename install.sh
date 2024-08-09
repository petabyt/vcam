set -e

cd ~
rm -rf /tmp/vcam
git clone https://github.com/petabyt/vcam /tmp/vcam
cd /tmp/vcam
make vcam
sudo cp vcam /usr/bin
