# Notes — ajustement mémoire GPU (VRAM) : à tester sur un GPU dédié

Statut : **ouvert**, rien de modifié dans le calcul pour l'instant.
À vérifier sur une machine avec un GPU dédié (idéalement NVIDIA/CUDA, et si
possible un modèle *sans* attention à fenêtre glissante, ex. Llama/Qwen/Mistral).

Code concerné : `src/localbackend.cpp` — `lb_fit_params()`,
`lb_estimate_kv_cache()`, `lb_pick_gpu()`.

---

## 1. La marge fixe de 2 Gio sur dGPU est peut-être trop agressive

`lb_fit_params()` retire une marge fixe de la mémoire libre avant d'ajuster :
512 Mio pour un iGPU, **2 Gio pour un dGPU**.

- Le calcul « au token près » ne concerne que le **prompt** (taille de
  `n_ctx`). L'**estimation mémoire**, elle, est heuristique : c'est cette
  imprécision que la marge absorbe.
- Une marge fixe est mal proportionnée : 25 % d'une carte de 8 Gio, 12 % d'une
  carte de 16 Gio. Sur les petites cartes, on déclenche trop tôt les
  réductions (n_ubatch, KV en Q8_0, mmproj sur CPU, déchargement de couches).
- Historique : avec 512 Mio de marge sur dGPU, il y a eu des plantages GPU OOM
  au chargement (commentaire dans le code). **Ne pas baisser la marge avant
  d'avoir fiabilisé l'estimation** (point 2).

## 2. Le facteur 0,26 sur le cache KV est propre à Gemma (risque d'OOM)

`lb_estimate_kv_cache()` multiplie la taille calculée par **0,26**. Le
commentaire l'attribue à un « layout compact » mesuré sur RTX 5070 Ti +
Gemma 4 12B.

Hypothèse : ce n'est pas un effet de layout mais l'**attention à fenêtre
glissante (SWA)** de Gemma. La plupart des couches ne gardent qu'environ
1024 tokens, seules quelques couches globales gardent tout le contexte. Le
ratio ≈ 0,26 correspond à ça.

Conséquence pour un modèle **sans SWA** : le cache KV réel est environ 4× plus
gros que l'estimation. Ordre de grandeur pour un 8B type Llama, 32k de
contexte, KV F16 : ~4,5 Gio réels contre ~1,2 Gio estimés, soit un écart d'environ
3,3 Gio, **plus que la marge de 2 Gio** → OOM probable. Aujourd'hui, la
marge masque en partie cette erreur.

Autres approximations :
- buffer de calcul : `n_ubatch × n_embd × n_gpu_layers × 8` octets (calibré
  à ~6, augmenté à 8 pour les buffers SWA de Gemma) ;
- +5 % sur le total.

## 2 bis. Sans flash attention, la matrice KQ n'était pas comptée (corrigé)

Mesuré le 2026-11-15 (RTX 5070 Ti 16 Go, Qwen3.5-9B Q4_K_M, hybride →
flash attention désactivée, n_ctx 43264, n_ubatch 2048) : 12 120 Mio
consommés au chargement du modèle contre 8 860 Mio estimés, puis CUDA OOM
pendant le décodage des images (mmproj chargé par-dessus).

Cause : sans flash attention, llama.cpp matérialise les scores d'attention
d'une couche, en F32 : `n_ctx × n_ubatch × n_head × 4` octets, soit 5408 Mio
ici (≈ 5360 mesurés). L'ancienne formule du buffer de calcul ignorait `n_ctx`.

Correction dans `lb_fit_params()` : ce terme est ajouté quand la flash
attention est désactivée, et la réduction 1 (`n_ubatch`) descend alors
jusqu'à 512 au lieu de 2048. Le relevé `[GPU-MEM]` (`InferenceEngine::
logGpuMemory`) reste en place pour les mesures suivantes.

## 2 ter. Flash attention activée pour les modèles hybrides (validé)

La flash attention était désactivée pour tous les modèles hybrides
(héritage de Jamba/Granite avec un ancien llama.cpp). Elle est maintenant
demandée (`AUTO`) pour eux aussi ; interrupteur : `kHybridFlashAttn` dans
`src/localbackend.cpp`.

Mesuré le 2026-11-15, même document (24 pages, 41 194 tokens, n_ctx 43264),
Qwen3.5-9B, RTX 5070 Ti :

| | Sans FA, ubatch 2048 | Sans FA, ubatch 512 | Avec FA, ubatch 2048 |
|---|---|---|---|
| Après chargement du modèle | 12 120 Mio | 7 735 Mio | 8 258 Mio |
| Pic pendant les images | OOM | ~9 040 Mio | 9 412 Mio |
| Estimation lb_fit (hors +5 %) | 10 013 Mio | 9 828 Mio | 10 513 Mio |

Résumé correct, mémoire stable d'image en image (+250 Mio au premier
encodage, puis plat). Avec FA, le cache V peut aussi être quantifié, donc
la réduction « KV en Q8_0 » est pleinement efficace pour ces modèles.

Attention : en `AUTO`, llama.cpp peut désactiver la FA sans le signaler
(logs INFO non transmis) si un backend ne la gère pas ; l'estimation serait
alors trop basse. Le signe : `[GPU-MEM] after model load` bien au-dessus de
l'estimation poids + KV + calcul.

## 2 quater. KV calculé couche par couche, marge ramenée à 1 Gio (fait)

Le facteur 0,26 est supprimé. `lb_read_kv_layout()` lit l'en-tête GGUF
(API `gguf.h`, les tableaux ne passent pas par `llama_model_meta_val_str`) :
têtes KV par couche, `full_attention_interval` (hybrides), motif et taille
de fenêtre glissante, `shared_kv_layers`, tailles de tête (et `_swa`).
Couche pleine : n_ctx cellules ; couche à fenêtre : min(n_ctx, n_swa +
n_ubatch), arrondis à 256. Structure inconnue → pleine attention partout
(surestimation).

Mesuré le 2026-11-15 (RTX 5070 Ti, même document de 24 pages, mode FULL),
tailles réelles lues dans les logs llama.cpp (`kv_cache: size`, `compute
buffer size`) et `[GPU-MEM]` :

| Modèle | KV estimé / réel | Calcul estimé / réel | Total estimé (hors +5 %) | Pic réel |
|---|---|---|---|---|
| Gemma 4 12B (SWA) | 1440 / 1440 | 2880 / 2188 | 11 064 | 10 319 |
| Qwen3-VL-8B (dense) | 3232 / 3232 | 2304 / 1763 | 10 325 | 9 272 |
| Qwen3.5-9B (hybride) | 1352 / 1352 | 2048 / 2173 | 9 958 | 9 342 |

L'ancien facteur aurait estimé le KV de Qwen3-VL à ~840 Mio (×3,8 trop
peu) et celui de Gemma 4 12B à 3954 Mio (×2,7 trop).

En conséquence, la marge dGPU passe de 2 Gio à `max(1 Gio, 6 % de la VRAM)`
(1 Gio aussi dans l'ajustement de llama.cpp). Avec 2 Gio, Qwen3-VL avait
dû passer le mmproj sur le CPU (76 s au lieu d'environ 20 s).

Corrigé au passage : `n_gpu_layers` compte aussi la couche de sortie dans
llama.cpp (couches GPU à partir de `n_layer + 1 - n_gpu_layers`). Passer le
nombre de couches laissait la couche 0 sur le CPU (KV de 24 Mio sur le CPU
pour Gemma, 90 Mio pour Qwen3-VL). On passe maintenant `n_gpu_layers + 1`.

Les lignes INFO de llama.cpp sur les tailles de buffers et la flash
attention sont désormais conservées dans `Hodeia.log` (`src/main.cpp`).

## 3. Plan de test / étapes proposées

1. **Mesurer avant de modifier.** Journaliser, pour chaque chargement :
   estimation (poids / KV / calcul / mmproj), mémoire libre avant le
   chargement, mémoire libre après le chargement et après la première
   inférence. Tester au moins :
   - Gemma (SWA) → valider le facteur 0,26 ;
   - un modèle sans SWA (Llama / Qwen / Mistral) à 16k et 32k de contexte →
     confirmer ou non la sous-estimation du KV.
2. **Rendre l'estimation exacte**, au choix :
   - lire `sliding_window` et la répartition des couches SWA/globales dans le
     GGUF, et calculer le KV par couche (supprimer le facteur 0,26) ;
   - ou, mieux, laisser llama.cpp faire un dry-run (`no_alloc`) du contexte
     pour obtenir la taille exacte des buffers par périphérique.
     (`common_fit_params` avait été écarté car il plantait en re-sondant les
     périphériques : passer par une autre voie.)
3. **Ensuite seulement, réduire la marge** : par exemple
   `max(512 Mio, 5 % de la VRAM totale)`, pour le pilote/la fragmentation.

---

## 4. Idée annexe : utiliser `device_id` (adresse PCI)

`ggml_backend_dev_props::device_id` = adresse PCI du périphérique
(ex. `0000:04:00.0`), pas l'identifiant vendor:device.

- **Usage utile** : repérer le même GPU exposé par CUDA *et* Vulkan (même
  adresse PCI). Aujourd'hui `lb_pick_gpu()` départage en préférant les noms
  qui commencent par « CUDA ». Comparer les `device_id` permettrait de compter
  la carte une seule fois, puis de choisir le backend.
- **Détection iGPU** : possible via sysfs (`/sys/bus/pci/devices/<id>/vendor`
  et `/device`), mais cela demande une table d'identifiants d'APU AMD et du
  code différent selon la plateforme (Linux uniquement simple). Le type
  renvoyé par le pilote reste la meilleure source → pas prioritaire.
- Ne **pas** déduire « intégré » de l'emplacement sur le bus (l'iGPU Intel est
  en `00:02.0`, mais un APU AMD Renoir est en `04:00.0`).

## 5. Rappel : détection iGPU (déjà modifiée)

`lb_is_integrated_gpu()` fait d'abord confiance au type renvoyé par le pilote
(`GGML_BACKEND_DEVICE_TYPE_IGPU` / `GPU`). L'heuristique sur le nom
(`lb_name_looks_integrated()`, appliquée à la description car le nom vaut
souvent `Vulkan0`) ne sert que de secours. Les CPU ne passent jamais par
l'heuristique.

Non couvert : Apple Silicon (Metal) passe par le chemin « GPU dédié » malgré
sa mémoire partagée. À revoir si Hodeia doit tourner sur Mac.
