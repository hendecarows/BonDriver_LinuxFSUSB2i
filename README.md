# BonDriver_LinuxFSUSB2i

Linux版[EDCB][link_edcb]で使用することを目的に、trinity19683氏が作成した[recfsusb2i][link_wiki]をLinux版BonDriver化したものです。

[IT917x][link_it9170]を採用した以下のデバイスに対応しています。

| Vendor | Product        |  Vid   |  Pid   | Remarks                     |
| :----- | :------------- | :----: | :----: | :-------------------------- |
| KEIAN  | [KTV-FSUSB2/V3][link_fsusb2v3] | 0x0511 | 0x0046 | 販売終了 S/N:K1212以降  |
| KEIAN  | [KTV-FSMINI][link_fsmini]     | 0x0511 | 0x0046 | 販売終了                    |
<!-- | ~~MyGica~~ | ~~[PT275][link_pt275]~~       | ~~0x048d~~ | ~~0x9175~~ |                             |
| ~~MyGica~~ | ~~[PT275C][link_pt275c]~~       | ~~0x048d~~ | ~~0xe275~~ |                             | -->

BonDriver化にあたり、[recfsusb2i][link_wiki]とnns779氏の[BonDriver_LinuxPTX][link_bonptx]のソースファイルをほぼそのまま使用しています。

## インストール

### ビルド

git, cmakeを含むビルドツールをインストールした状態でビルドします。

```console
git clone https://github.com/hendecarows/BonDriver_LinuxFSUSB2i.git
git submodule update --init --recursive
cd BonDriver_LinuxFSUSB2i
mkdir build
cd build
cmake ..
make
```

### インストール

必要に応じてユーザーをvideoグループに追加し、再ログインします。
```console
sudo adduser $USER video
```
udevルールを反映しデバイスを再接続します。
```console
sudo cp ../99-fsusb2i.rules /etc/udev/rules.d
sudo udevadm control --reload
sudo udevadm trigger
```

BonDriver_LinuxFSUSB2i.so,iniを`/usr/local/lib/edcb`にコピーします。

```console
sudo cp BonDriver_LinuxFSUSB2i.so /usr/local/lib/edcb
sudo cp ../BonDriver_LinuxFSUSB2i.ini /usr/local/lib/edcb
```

EpgDataCap_Bonでチャンネルスキャンします。

```console
EpgDataCap_Bon -d BonDriver_LinuxFSUSB2i.so -chscan
```

## ライセンス

`recfsusb2i/`ディレクトリ内が[recfsusb2i][link_wiki]由来のコード、`src/`ディレクトリ内が[BonDriver_LinuxPTX][link_bonptx]由来のコードです。

[recfsusb2i][link_wiki]のライセンスは`readMe.txt`内の`based on GPLv3`との記述からGPLv3と判断します。また、[BonDriver_LinuxPTX][link_bonptx]のライセンスは`MIT`です。

[link_wiki]: https://ktvwiki.22web.org/?BonDriver_FSUSB2i&i=1
[link_edcb]: https://github.com/xtne6f/EDCB
[link_bonptx]: https://github.com/nns779/BonDriver_LinuxPTX
[link_it9170]: https://www.ite.com.tw/en/product/cate4/IT9170
[link_fsusb2v3]: https://www.keian.co.jp/archives/products/ktv-fsusb2v3
[link_fsmini]: https://www.keian.co.jp/archives/products/ktv-fsmini
[link_pt275]: https://www.mygica.com/product/isdbt-tuner/
[link_pt275c]: https://shop.geniatech.com/product/pad-tuner/?wpam_id=2