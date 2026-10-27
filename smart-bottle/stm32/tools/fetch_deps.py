#!/usr/bin/env python3
"""Descarcă driverele ST necesare (HAL, CMSIS, BSP pentru STM32F429I-DISC1) în stm32/Drivers
și fonturile LCD în stm32/Utilities.

Versiunile sunt fixate pe commit-urile folosite de STM32CubeF4 la data proiectului, deci
build-ul e reproductibil. Rulează o singură dată (sau după ce ștergi Drivers/):

    python tools/fetch_deps.py

Are nevoie doar de git. Driverele sunt licențiate de ST (BSD-3 / Apache-2.0, vezi LICENSE din fiecare).
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
DRV = os.path.join(ROOT, 'Drivers')
GH = 'https://github.com/STMicroelectronics/'

# cale în Drivers/  ->  (repo, commit)
REPOS = {
    'CMSIS/Device/ST/STM32F4xx':      ('cmsis_device_f4',      '9192c7b9df75a142f2027ab266601fe061fc00b3'),
    'STM32F4xx_HAL_Driver':           ('stm32f4xx_hal_driver', '1f6451c3e07728b4c830744de380e56bf5bc0026'),
    'BSP/STM32F429I-Discovery':       ('32f429idiscovery-bsp', 'd77779a2a4e950246752a4397e2adf40c25829f9'),
    'BSP/Components/Common':          ('stm32-bsp-common',     '1e18c5afdf1f5971a35c8e2f88b6a21e5568ed92'),
    'BSP/Components/ili9341':         ('stm32-ili9341',        'c44184fc674dcea7f52597994ad009627d475186'),
    'BSP/Components/l3gd20':          ('stm32-l3gd20',         '948eef083e7347e77ca76df32080d0c759fda320'),
    'BSP/Components/stmpe811':        ('stm32-stmpe811',       '9ef6302330ad54468210bbb8e2e9b502c9558bb8'),
}
# Din STM32CubeF4 propriu-zis (nu sunt submodule): nucleul CMSIS și fonturile pentru LCD.
CUBE = ('STM32CubeF4', '60c8abe0ae88a23bbd1c925358330031e36dd20f')
# relativ la stm32/: BSP-ul include fonturile prin ../../../Utilities/Fonts
CUBE_DIRS = {'Drivers/CMSIS/Include': 'Drivers/CMSIS/Include', 'Utilities/Fonts': 'Utilities/Fonts'}


def git(*args, cwd=None):
    subprocess.run(['git', *args], cwd=cwd, check=True)


def fetch(repo, sha, dest, sparse=None):
    os.makedirs(dest, exist_ok=True)
    git('init', '-q', cwd=dest)
    git('remote', 'add', 'origin', GH + repo + '.git', cwd=dest)
    if sparse:
        git('config', 'core.sparseCheckout', 'true', cwd=dest)
        with open(os.path.join(dest, '.git', 'info', 'sparse-checkout'), 'w') as f:
            f.write('\n'.join('/' + p + '/' for p in sparse) + '\n')
        git('fetch', '-q', '--depth', '1', '--filter=blob:none', 'origin', sha, cwd=dest)
    else:
        git('fetch', '-q', '--depth', '1', 'origin', sha, cwd=dest)
    git('checkout', '-q', 'FETCH_HEAD', cwd=dest)
    shutil.rmtree(os.path.join(dest, '.git'), ignore_errors=True)


def main():
    # consola Windows (cp1252) nu poate afișa diacriticele
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    if os.path.isdir(DRV) and '--force' not in sys.argv:
        print('Drivers/ există deja. Folosește --force ca să-l descarci din nou.')
        return
    shutil.rmtree(DRV, ignore_errors=True)
    shutil.rmtree(os.path.join(ROOT, 'Utilities'), ignore_errors=True)
    for path, (repo, sha) in REPOS.items():
        print(f'-> {repo}')
        fetch(repo, sha, os.path.join(DRV, path))
    print('-> STM32CubeF4 (CMSIS core, fonturi)')
    tmp = os.path.join(DRV, '_cube')
    fetch(CUBE[0], CUBE[1], tmp, sparse=list(CUBE_DIRS))
    for src, dst in CUBE_DIRS.items():
        os.makedirs(os.path.dirname(os.path.join(ROOT, dst)), exist_ok=True)
        shutil.move(os.path.join(tmp, src), os.path.join(ROOT, dst))
    shutil.rmtree(tmp, ignore_errors=True)
    print('Gata: Drivers/ e pregătit. Acum: make')


if __name__ == '__main__':
    main()
