# -*- coding: utf-8 -*-
# ============================================================================
#  logic/config.py  -  config.local.py の読み込み (server/ 直下)
# ============================================================================
import os
import importlib.util

_cfg = None


def cfg():
    global _cfg
    if _cfg is None:
        server_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        path = os.path.join(server_dir, "config.local.py")
        if not os.path.isfile(path):
            raise RuntimeError("config.local.py がありません (config.example.py からコピーして設定)")
        spec = importlib.util.spec_from_file_location("wlb_config_local", path)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        _cfg = mod.CONFIG
    return _cfg
