#!/bin/sh
# Copiază logica comună din aplicație în funcția Supabase (Deno nu vede folderul web/ la deploy).
cd "$(dirname "$0")" && cp ../web/insights.js functions/hydrogpt/insights.js && echo "insights.js copiat"
