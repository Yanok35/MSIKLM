
# Create initial debian package:

Following [handbook informations](https://debian-handbook.info/browse/fr-FR/stable/sect.building-first-package.html):

```
mkdir -p pkg/msiklmd-0.1
cd pkg/msiklmd-0.1
dh_make --native -s -y
```

## Update debian/rules and debian/control files:

```
sed -i 's/dh $@/dh $@ --with=systemd/' debian/rules
sed -i 's/debhelper (>= 10)/debhelper (>= 10), libhidapi-dev, dh-systemd (>= 1.3)/' debian/control
```

## Add link to systemd service:

```
ln -s ../../../msiklmd.service debian/
cp ../../msiklmd .
echo 'msiklmd usr/bin/' > debian/msiklmd.install
```

## Build deb package

```
debuild -us -uc
```

## Verify package content:

```
cd ..
dpkg -I msiklmd_0.1_amd64.deb
dpkg -x msiklmd_0.1_amd64.deb dump && ls -lR dump
```