# Cahier des charges V2.5 — Entropy Piano Tuner (Qt 6) : amélioration acoustique et algorithmique

*Version consolidée du 15/11/2026. Statuts des constats techniques : **Vérifié** = lu directement dans le code ou dans un fichier .ept ; **Scan** = issu du scan du code, à confirmer par le banc d'essai.*

**En bref (tout le projet).** Entropy Piano Tuner calcule la hauteur idéale de chaque note d'un piano à partir de son enregistrement. Ce cahier des charges décrit comment rendre ce calcul plus juste (mesures plus précises), reproductible (même résultat à chaque lancement) et plus complet (exploration plus poussée, point de rupture du chevalet, journal des essais). Le temps de calcul n'est pas une contrainte : jusqu'à environ 45 minutes sont acceptables entre le scan du piano et l'accord.

## 0. Contexte et règles de travail

**En bref.** Où se trouve le code, comment on le modifie sans casser l'existant, et comment on nomme les notes.

- Dépôt : https://github.com/smcoppola/Entropy-Piano-Tuner-Qt6 (fork Qt 6 de levush/Entropy-Piano-Tuner, licence GPL v3).
- Environnement : Qt 6.10+, MSVC 2022 (qmake via build\_project.bat), édition sous VS Code / Visual Studio.
- Fichiers audités : signalanalyzer.cpp, fftanalyzer.cpp, fftadapter.h (acquisition et FFT) ; key.cpp, piano.cpp, keyboard.h, mathtools.cpp (modèle et maths) ; auditorypreprocessing.cpp, entropyminimizer.cpp, entropyminimizer.xml (prétraitement et moteur) ; mainwindow.cpp, calculationprogressgroup.cpp, algorithmparameters.cpp (interface et paramètres).
- Lus directement lors des revues : entropyminimizer.cpp et auditorypreprocessing.cpp (branche main), et un fichier .ept. Les autres constats viennent du scan.
- **Principe directeur** :
  1. Le module historique `entropyminimizer` reste intact : c'est la référence de non-régression.
  2. Le nouveau module `entropyminimizerv2` a sa **propre copie** du prétraitement (auditorypreprocessing). Les corrections 1.2 à 1.6 s'y appliquent, jamais à l'ancien module.
  3. Le code partagé (FFT, signal, maths) n'est modifié que derrière un paramètre dont la valeur par défaut est l'ancien comportement. Si c'est impossible, on note que l'ancien moteur en bénéficie.
  4. Les essais sur l'optimiseur (données figées) et ceux sur la FFT (signaux synthétiques, nouveaux scans) sont menés séparément, pour savoir ce qui a changé quoi.
- **Notation des notes** : notation scientifique (A4 = 440 Hz, soit La3 en notation française). **Un piano n'a pas toujours 88 touches** : le nombre de touches N et l'indice de A4 (mKeyNumberOfA4) viennent des paramètres du piano. Les notes sont donc exprimées par rapport à A4 (F3 = A4−16, F4 = A4−4). Les numéros de touche donnés en exemple (A0 = 0, F3 = 32, F4 = 44, A4 = 48) ne valent que pour un piano de 88 touches commençant à A0. Contrainte vue dans le code : ComputeInitialTuningCurve s'arrête si mKeyNumberOfA4 ≤ 13 ou si N − mKeyNumberOfA4 ≤ 13.
- **Reprise avec l'assistant** : il ne peut ouvrir que les liens collés dans le message (de préférence les liens Raw). Joindre ce document et les liens des fichiers à lire.

## 1. Objectifs

**En bref.** Sept objectifs, de la mesure du son jusqu'au suivi des essais.

1. Éliminer les biais de mesure amont : échelle FFT, estimation sous-bin des fréquences, estimation de l'inharmonicité B.
2. Supprimer la quantification et l'aléatoire : résolution continue (0,1 cent) et optimisation déterministe.
3. Explorer efficacement le paysage d'entropie : évaluation incrémentale, puis recherche multi-niveaux, sans contrainte de temps.
4. Respecter la mécanique du piano : point de rupture (Break), régularisation de courbure sans brider l'étirement, indicateur du gain d'entropie.
5. Rester ancré sur l'accord tempéré théorique étiré (courbe initiale de l'ancien module) : on n'invente pas un autre tempérament (voir 3.8).
6. Générer plusieurs candidats initiaux par construction progressive imitant l'accordeur (voir 3.9). L'entropie reste le seul critère.
7. Journaliser les essais pour les comparer et les reproduire (voir 3.10).

## 2. Constats techniques consolidés

**En bref.** Ce que nous avons trouvé dans le code actuel : chaque ligne donne le défaut, le degré de certitude et le lot qui le traite.

| Réf | Fichier / fonction | Constat | Statut | Lot |
| --- | --- | --- | --- | --- |
| C1 | entropyminimizer.cpp (mPitch, modifySpectralComponent, getPitchET440) | mPitch est un std::vector\<int> ; la fréquence enregistrée (getRecordedPitchET440AsInt) et la retouche manuelle de la courbe (getPitchET440) sont arrondies à l'entier : pas de 1 cent, jusqu'à ±0,5 cent d'erreur de placement. | Vérifié | 3.1, 3.6 |
| C2 | entropyminimizer.cpp (computeEntropy, addToAccumulator) | À chaque essai : copie de l'accumulateur, parcours total, normalisation et logarithme sur tous les bins. | Vérifié | 3.2 |
| C3 | entropyminimizer.cpp (minimizeEntropy) | Monte-Carlo à température nulle, graine aléatoire, arrêt heuristique (pbVel) : minima locaux et résultat variable selon la graine. | Vérifié | 3.3 |
| C4 | auditorypreprocessing.cpp (performAuditoryPreprocessing) | cleanSpectrum s'exécute avant extrapolateInharmonicity : il utilise donc un B potentiellement faux. | Vérifié | 1.2 |
| C5 | auditorypreprocessing.cpp (cutLowFrequencies) | Calcule 5·ftom(f)/6 au lieu de ftom(5f/6). | Vérifié | 1.6 |
| C6 | auditorypreprocessing.cpp (improveHighFrequencyPeaks) | À partir de A4, écrase les partiels 2 à 6 par un modèle synthétique. Le facteur vaut 0 à A4 : les partiels y sont mis à 0. Aucun test à 5 kHz malgré le commentaire. | Vérifié | 1.4 |
| C7 | auditorypreprocessing.cpp (applyMollifier) | Largeur df = 55/f + f/2000 : environ 122 cents à A0, environ 1 cent au-dessus de 400 Hz. | Vérifié | 1.5, 3.5 |
| C8 | fftanalyzer.cpp, fftadapter.h | Échelle FFT : b = 2·fft.size()/samplingRate au lieu de la vraie longueur N ; biais jusqu'à environ 0,5 cent sur fenêtres courtes. | Scan | 1.0 |
| C9 | fftanalyzer.cpp | Retourne un bin entier sans interpolatePeakPosition() ; estimateFrequencyShift() est calculé mais jamais appliqué. | Scan | 1.0 |
| C10 | fftanalyzer.cpp (estimateInharmonicity) | Balayage grossier par pas de 3 % (scan\_B \*= 1.03), poids égal pour tous les partiels même bruités. | Scan | 1.3 |
| C11 | signalanalyzer.cpp | Fenêtre de Hann commentée, remplacée par un fondu linéaire de 2 % : fuite spectrale. | Scan | 1.7 |
| C12 | keyboard.h, piano.cpp | mNumberOfBassKeys existe (défaut 28) ; Piano::getDefiningTempFrequency accepte déjà un double en cents. | Scan | 1.3, 2.1, 3.6 |
| C13 | entropyminimizer.cpp (ComputeInitialTuningCurve, getTolerance) | Courbe initiale heuristique (interpolation linéaire A3–A5, poids fixes 0,3/0,7) et tolérance par touche, calculée par rapport à A4 (formule cubique) : ±5 à A4, ±8 à A3, ±7 à A5, ±15 à A2 et A6, ±30 à A0 et A7, environ ±35 à C8. La tolérance est ignorée par les mouvements de section et par une touche déjà hors tolérance. | Vérifié | 3.8 |
| C14 | fichiers .ept | Contient uniquement les résultats d'analyse (recordedFrequency, measuredInharmonicity, computedFrequency) et le spectre logarithmique de 10 800 bins (`<spectrum>`), sans le son brut ni la FFT linéaire. Fréquences écrites avec 6 chiffres significatifs. | Vérifié | 0.1, 3.6 |

## 3. Spécifications fonctionnelles et techniques

### Lot 0 — Jeu de référence, mesures de base et journal

**En bref.** Avant de modifier quoi que ce soit, on mesure comment l'ancien moteur se comporte, pour pouvoir dire ensuite si on s'améliore.

- **0.1 Jeu de référence** : au moins 3 pianos enregistrés (droit et queue), sauvegardés en .ept, plus des signaux synthétiques contrôlés (f0 et B connus, bruit, doublets d'unissons).
- **0.1 bis Plan d'essais (conséquence de C14)** : le .ept ne contient pas le son brut, et on ne conserve pas de fichiers WAV. Donc : (1) le prétraitement et l'optimiseur de la v2 se testent sur les anciens .ept, en comparaison avec l'ancien moteur ; (2) les correctifs FFT (1.0) se valident sur les signaux synthétiques, puis sur de nouveaux scans ; (3) le cycle complet (scan puis calcul) se teste sur de nouveaux scans de piano. Comparer l'ancienne et la nouvelle FFT sur deux scans différents mélangerait la correction et la variabilité d'un scan à l'autre : c'est le rôle des signaux synthétiques.
- **0.2 Mesure de base** : exécuter l'algorithme historique avec plusieurs graines ; relever la dispersion par touche (en cents), l'entropie finale et le temps. Conserver les courbes : elles servent de point de départ optionnel (3.3).
- **0.3 Journal des essais (3.10) mis en place dès cette phase**, pour que toutes les mesures soient comparables.

### Lot 1 — Analyse FFT et prétraitement spectral

**En bref.** On améliore la mesure du son (fréquences, inharmonicité) et le nettoyage des spectres avant le calcul.

- **1.0 Échelle FFT et sous-bin de f1** (derrière un paramètre, ancien comportement par défaut) : stocker la longueur exacte N\_fft dans FFTData (fq = q·Fs/N\_fft) ; activer l'interpolation parabolique sous-bin sur ln(amplitude) dans findAccuratePeakFrequency() ; tester estimateFrequencyShift(). Valider d'abord sur signaux synthétiques (balayage de fréquences et de durées) : ne corriger que ce que le banc confirme. Le biais de l'interpolation dépend du fenêtrage (1.7).
- **1.1 Pondération dBA** : conserver la courbe existante de convertToSPLA.
- **1.2 Ordre du prétraitement** *(copie v2)* : exécuter extrapolateInharmonicity() avant cleanSpectrum().
- **1.3 B fin et Break** *(copie v2)* : par touche, recherche en deux étapes (balayage grossier puis optimisation fine) avec pondération des partiels par rapport signal/bruit ; entre touches, ajustement robuste de ln B (Huber ou Theil-Sen) en deux segments, cordes filées \[0 .. breakKeyIndex−1\] et cordes nues \[breakKeyIndex .. N−1\]. breakKeyIndex vaut par défaut mNumberOfBassKeys mais reste modifiable (le pont des basses n'est pas forcément la limite filées / nues) ; à valider en traçant ln B selon la touche. 0 = désactivé. Moins de 6 valeurs valides dans un segment : repli sur getExpectedInharmonicity(f1).
- **1.4 improveHighFrequencyPeaks** *(copie v2)* : comparer (a) comportement actuel, (b) désactivé, (c) complément uniquement au-dessus de 5 kHz via max(mesuré, synthétique), sans jamais écraser les données réelles ni annuler A4. Choisir sur données.
- **1.5 Échelle du mollificateur** *(copie v2)* : applyMollifier(key, scale), scale = 1 reproduit l'actuel. Largeur bornée dans les graves (valeur à fixer sur données). Conserver le spectre avant mollification (environ N × NumberOfBins doubles).
- **1.6 Coupure basse** *(copie v2)* : remplacer 5·ftom(f)/6 par ftom(5.0·f/6.0). À noter dans la non-régression.
- **1.7 Fenêtrage temporel** : fenêtre paramétrable pour comparer, sur signaux synthétiques, le fondu actuel (2 %), Tukey (α entre 0,1 et 0,2), Hann et Blackman-Harris.

### Lot 2 — Interface, paramètres et indicateur d'entropie (Qt 6 / Qwt)

**En bref.** Ce que l'utilisateur voit et règle : le point de rupture, quelques paramètres, un indicateur de progression.

- **2.1 Point de rupture** : champ « Index de la touche de rupture » connecté à mNumberOfBassKeys ; ligne verticale QwtPlotMarker (style VLine) sur le graphique Qwt. Fichiers anciens : champ absent = comportement actuel.
- **2.2 Audit de la pondération psychoacoustique (avant toute extension)** : déjà établi : conversion dBA (courbe voisine de 40 phones) et seuil dur I0 = 1e-7 appliqué sur un spectre normalisé par touche ; pas de renormalisation après SPLA, donc un poids relatif des touches implicite. Reste à confirmer dans convertToSPLA et initializeSPLAFilter qu'aucune autre correction fréquentielle n'existe. Pas d'égaliseur 6 bandes ni de nouvelle pondération tant que l'audit n'a pas montré qu'une fonction manque et que son effet n'est pas validé expérimentalement (piste préférable à un égaliseur : rendre le niveau de la courbe et le seuil paramétrables).
- **2.3 Paramètres XML du plug-in** (entropyminimizerv2.xml) : budget de temps, nombre de cycles sans gain, poids de régularisation λ, fenêtre de recherche W, nombre de termes atténués au Break, calendrier multi-échelle, tolérance autour de la courbe initiale. Les constantes de la construction progressive (3.9) restent en dur dans le code.
- **2.4 Indicateur H\_rec et H\_courbe** : bouton dans CalculationProgressGroup qui calcule l'entropie de l'enregistrement tel quel (décalage nul) et celle de la courbe courante, avec la même chaîne de prétraitement ; mise à jour à chaque retouche manuelle (MSG\_CHANGE\_TUNING\_CURVE) via l'accumulateur incrémental ; ΔH = H\_courbe − H\_rec. Objectif à mesurer (pas une exigence) : moins de 1 ms par retouche. C'est un indicateur de progression, pas une mesure indépendante de la qualité de l'accord (c'est l'objectif de l'optimiseur) ; comparable seulement pour un même piano et les mêmes réglages.

### Lot 3 — Cœur de l'optimiseur (entropyminimizerv2)

**En bref.** On remplace le tirage au sort par une recherche systématique, plus précise, et on ajoute un point de départ construit comme le ferait un accordeur.

- **3.1 Positionnement fractionnaire exact (0,1 cent)** : mPitch en double ; décalage exact = décalage entier + résidu fractionnaire constant par touche δ\_k = R\_k − rec\_k (R\_k = arrondi de la fréquence enregistrée en cents, δ\_k entre −0,5 et +0,5 cent), intégré dans le mollificateur : S'(m) = Σ c\[ms\]·w(ms − m + δ\_k) / norme. Aucun flou d'interpolation linéaire. Prérequis confirmé : les bins sont indexés en fréquence absolue (Key::IndexToFrequency ; 10 800 bins, soit 9 octaves de bins d'un cent). Dans la v2, getPitchET440 (retouche manuelle, arrondie à l'entier aujourd'hui) et getRecordedPitchET440AsInt passent aussi en double.
- **3.2 Objectif unique et entropie incrémentale** : J(x) = H\_globale(x) + λ·Σ w\_k·(x\_{k+1} − 2·x\_k + x\_{k−1})², H étant l'entropie de Shannon de la superposition des N spectres. H = ln S − T/S avec S = Σ A\_j et T = Σ A\_j·ln A\_j. Minimiser H revient à maximiser l'interaction entre partiels de touches différentes : seuls les bins où plusieurs touches se recouvrent les couplent. Spectres prétraités stockés sous forme creuse ; mise à jour en O(bins non nuls) ; recalcul de T de zéro à la fin de chaque balayage, avec contrôle de concordance à tolérance relative (environ 1e-9).
- **3.3 Optimisation globale finale (VNS déterministe)** : s'applique à un candidat complet de N notes ; distincte de la construction de 3.9.
  - Règle d'or : garder la meilleure solution connue (bestPitch, bestJ) ; toute exploration qui dégrade J est rejetée.
  - Point de départ : la courbe initiale (3.8) ; options : un candidat de 3.9, le résultat de l'ancien Monte-Carlo, ou la courbe aux moindres carrés sur les coïncidences de partiels.
  - Niveau 1 : descente par coordonnées, fenêtre \[−W, +W\] par touche, minimum exact, ordre de parcours déterministe alterné (de A4 vers les extrémités puis l'inverse).
  - Niveau 2 : blocs harmoniquement couplés (octaves k et k+12, quintes k et k+7, douzièmes k et k+19, triplets k, k+7, k+12), pas égal à la moitié de la largeur du mollificateur de l'échelle courante, grille de 3 valeurs par touche puis affinage. Évolution possible : graphe d'interactions calculé à partir des partiels réels et des B mesurés.
  - Niveau 3 : translations de section (touches ≤ k ou ≥ k) et recherche locale itérée avec une suite fixe de perturbations plus grandes que le mollificateur (par exemple blocs d'octaves de ±5 cents), puis descente complète.
  - La tolérance de 3.8 s'applique à tous les mouvements. Arrêt : un cycle complet sans amélioration (ΔJ inférieur à 1e-10), borné par le budget de temps et l'annulation (cancelThread()).
- **3.4 Régularisation de courbure** : pénalité sur la dérivée seconde, λ configurable (défaut 0 pour comparaison brute). Au Break (entre les touches b−1 et b), on atténue les deux termes qui le chevauchent, c'est-à-dire les motifs (b−2, b−1, b) et (b−1, b, b+1), soit 4 touches concernées ; le nombre de termes atténués reste réglable. Pas de borne dure sur la variation entre notes (la pente naturelle atteint 1 à 2 cents par touche aux extrémités).
- **3.5 Multi-échelle** : recherche sur un calendrier décroissant d'échelles du mollificateur, chaque niveau initialisant le suivant (par exemple 32, 16, 12, 8, 6, 4, 3, 2, 1). L'entropie n'est comparable qu'au sein d'une même échelle.
- **3.6 Raffinement sub-cent** : à l'échelle 1, grille locale de pas 0,1 cent via le décalage fractionnaire de 3.1, puis transmission de p + δ à Piano::getDefiningTempFrequency (qui accepte déjà un double). L'interpolation parabolique n'est utilisée que si l'entropie est lisse à l'échelle du cent. Précision du fichier : les fréquences du .ept sont écrites avec 6 chiffres significatifs, soit une résolution de 0,002 à 0,017 cent selon la fréquence, suffisante pour 0,1 cent ; vérifier à l'implémentation (modules/core/file/, non lu à ce jour) que le format d'écriture XML et les arrondis d'interface ne re-quantifient pas l'accordage raffiné.
- **3.7 Architecture plug-in** : créer modules/algorithms/entropyminimizerv2/, enregistré via ALGORITHM\_CPP\_START, avec son XML de description, sélectionnable dans l'interface. Le bloc `<algorithms current>` à la fin du .ept indique que le format prévoit un algorithme courant et ses paramètres XML : avant de conclure qu'aucune modification du format n'est nécessaire, tester l'ouverture dans l'ancienne version d'un .ept enregistré avec entropyminimizerv2, et l'inverse.
- **3.8 Tolérance, courbe initiale et ancrage au tempérament théorique**
  - **En bref** : la courbe initiale de l'ancien module (accord tempéré théorique avec l'étirement calculé à partir de B) reste la référence et le garde-fou. On n'invente pas un autre tempérament.
  - Décision : conserver la courbe initiale et la tolérance de l'ancien module (C13), appliquée de façon cohérente à tous les mouvements et aux touches déjà hors tolérance. Tolérance paramétrable (2.3).
  - L'ancrage reste cette courbe de référence pendant la construction progressive (3.9) et l'optimisation globale (3.3) : jamais le candidat en cours.
  - A4 reste à la fréquence demandée par l'utilisateur et constitue l'ancrage fixe de toutes les phases.
  - Mouvement par classe de hauteur (décalage commun de toutes les touches de même nom) : **non retenu**, car il modifierait le tempérament.
  - Test de dérive facultatif : mesurer l'effet d'une tolérance élargie sans l'adopter si le résultat s'éloigne du tempérament théorique (quintes ou tierces plus pures).
  - Amélioration possible de la courbe initiale par moindres carrés sur les coïncidences de partiels, sans changer sa nature.
- **3.9 Construction progressive de candidats (imitation de l'accordeur)**
  - **En bref** : au lieu de partir d'une seule courbe, on fabrique plusieurs candidats comme le ferait un accordeur : d'abord l'octave de tempérament autour du La 440, puis report octave par octave. Chaque candidat est ensuite amélioré par l'optimisation globale sur les N notes (3.3), qui reste le juge final.
  - Rôle : générer des candidats initiaux. Aucune modification de l'entropie globale ni de l'optimiseur final.
  - Critère unique : l'entropie de la superposition des notes déjà intégrées. Aucune règle de battement ni d'objectif intervallique : les méthodes manuelles ne fournissent que l'**ordre** d'ajout des notes. Les entropies d'étapes différentes ne sont pas comparables ; la comparaison décisive se fait après intégration des N touches et optimisation globale, avec la même fonction objectif.
  - **Étape 1, octave de tempérament** : A4 fixe ; 13 notes de F3 (A4−16) à F4 (A4−4), dans l'ordre d'une séquence (Annexe A). Fenêtre de positions : la tolérance de l'ancien module pour chaque touche, autour de la courbe de référence (±10 à F3, ±8 à A3, ±7 à C4, ±6 de C#4 à E4, ±5 à F4), soit de 11 à 21 positions par note sur une grille de 1 cent, puis affinage de ±1 cent par pas de 0,1. Groupes de 4 à 5 notes testés exhaustivement (au plus environ 129 000 combinaisons pour 4 notes et 2,2 millions pour 5 notes, d'après la fenêtre de chaque touche) ; le reste par recherche en faisceau. Les 13 notes d'un seul coup représenteraient environ 2,5×10^15 combinaisons. Cette zone est peu étirée : si le journal signale des positions retenues au bord de la fenêtre, on étudie un élargissement. Si le clavier ne contient pas toute la zone, voir Questions ouvertes.
  - **Étape 2, report par octaves** : monter et descendre alternativement d'une octave. Chaque note de la nouvelle octave part de la position reportée depuis la note fixée une octave plus loin (étirement compris), dans la fenêtre de tolérance de sa touche autour de la courbe de référence (±5 près de A4, jusqu'à ±30 à A0 et A7 et environ ±35 à C8). Groupes d'une ou deux notes.
  - **Étape 3, extrémités et Break** : grave et aigu jusqu'aux N touches. La régularisation de 3.4 ne porte que sur les motifs dont les trois notes sont déjà intégrées.
  - **Fixation stricte** : une note validée devient fixe pour la suite de la construction ; aucune retouche rétroactive. Le risque de dépendance à l'ordre est à mesurer.
  - **Faisceau** : conserver plusieurs meilleurs candidats par étape. Égalités départagées de façon déterministe (indice de touche le plus bas, puis position la plus basse).
  - **Paramètres en dur** : séquence, taille de groupe, pas, fenêtre et largeur de faisceau sont des constantes nommées regroupées dans une structure unique, enregistrées par le journal (3.10).
  - **Critère de succès** : garder 3.9 seulement si l'entropie finale après 3.3 est plus basse, ou la robustesse meilleure (dispersion plus faible), que celle obtenue en partant de la courbe initiale ou du résultat de l'ancien Monte-Carlo. Si tous les candidats convergent vers le même résultat, c'est un gain de robustesse, pas d'accord.
  - Idée facultative à tester plus tard : garder libres les 3 ou 4 dernières notes ajoutées au lieu de la fixation stricte (à la 6e note, les notes 3 à 6 libres, les notes 1 et 2 fixes), seulement si la version stricte montre une dépendance à l'ordre.
- **3.10 Journal des essais**
  - **En bref** : chaque exécution écrit un fichier structuré qui permet de comparer les essais et de les reproduire.
  - Pour chaque exécution : identifiant unique, version du moteur, paramètres et constantes, données d'entrée, séquence utilisée.
  - Pour chaque étape : notes ajoutées et déjà fixes, positions explorées, pas et fenêtre, nombre de combinaisons évaluées, entropie avant et après, position retenue (avec un signalement du bord de fenêtre), nombre de candidats conservés, durée.
  - Pour chaque candidat complet : entropie à la fin de la construction, résultat et entropie après optimisation globale, courbe finale, durée totale, statut d'arrêt.
  - Format structuré exploitable automatiquement (JSON ou CSV). Le journal ne doit ni modifier le résultat ni introduire de non-déterminisme ; son coût est mesuré.

### Lot 4 — Banc d'essai et validation

**En bref.** On vérifie, avec des mesures et à l'oreille, que la nouvelle version est réellement meilleure.

- Export CSV des courbes ; mesure de précision sur signaux synthétiques (erreur de f0 et de B).
- Déterminisme : dispersion entre exécutions inférieure à 0,01 cent pour une même version compilée sur une même machine ; documenter l'écart éventuel entre machines ou compilateurs.
- Indicateur (H\_rec, H\_courbe, ΔH) ; critères indépendants : taux de battement et régularité (dérivée seconde des battements) sur octaves, quintes, tierces et sixtes ; comportement au Break ; écart à la courbe de référence (3.8).
- Comparaison des candidats de 3.9 : mêmes données, mêmes paramètres, critère de succès de 3.9.
- Validation acoustique : tests A/B (algorithme original contre v2) sur le jeu de référence, et écoute à l'aveugle sur piano réel.

## 4. Critères et contraintes

**En bref.** Les conditions pour considérer le travail réussi.

- **Non-régression** : v2 ne reproduira pas exactement l'ancien résultat (l'ancien est aléatoire, et la correction de C1 déplace légitimement les spectres). Critère : écart RMS à la courbe moyenne de l'original inférieur ou comparable à la dispersion de l'original entre graines (mesure 0.2).
- **Performance** : pas de contrainte ; jusqu'à environ 45 minutes acceptables. Budget de temps réglable, progression fiable, annulation possible.
- **Compatibilité** : anciens fichiers .ept chargeables (champs ajoutés optionnels) ; ouverture croisée des .ept entre ancienne version et v2 à tester (3.7). Pianos de N touches quelconque (voir section 0). Qt 6.10+, MSVC 2022.
- **Licence** : GPL v3 ; les versions modifiées distribuées restent sous GPL v3.

## 5. Risques

**En bref.** Ce qui peut mal tourner, pour y penser à l'avance.

- Changements FFT qui brouillent la comparaison avec l'optimiseur si les essais sont mélangés.
- Modèle de B par segment non validé ; pont des basses différent de la limite filées / nues.
- Grille grossière inadaptée à la largeur du mollificateur (3.3).
- Dépendance au chemin de construction (3.9) : comparer plusieurs séquences et conserver plusieurs candidats.
- Explosion combinatoire si trop de notes sont ajoutées à la fois : contrôler la taille des groupes, la fenêtre et la largeur du faisceau.
- Fenêtre issue de la tolérance de l'ancien module trop étroite : repérer les bords de fenêtre dans le journal.
- Pianos non standard (moins de 88 touches, A4 à un autre indice) : zone F3–F4 incomplète, indices en dur à proscrire.
- Régularisation trop forte qui lisse des irrégularités réelles ; déterminisme à la précision des flottants seulement.

## 6. Plan de réalisation incrémental

**En bref.** L'ordre des étapes, de la mesure de base à la validation.

1. **Phase 0** (Lot 0) : scan complémentaire (mécanisme de plug-in, lecture de modules/core/file/), jeu de référence, mesures de base de l'ancien moteur, journal (3.10).
2. **Phase 1** : squelette du module entropyminimizerv2 (3.7) avec copie du prétraitement ; corrections 1.2 et 1.6 dans cette copie ; correctifs FFT 1.0 derrière un paramètre, testés à part sur signaux synthétiques.
3. **Phase 2** (3.1, 3.2, 2.4) : positionnement fractionnaire, accumulateur incrémental, indicateur H\_rec et H\_courbe.
4. **Phase 3** (3.3, 3.4, 3.8) : optimisation globale (niveaux 1 à 3), régularisation, tolérance cohérente.
5. **Phase 3 bis** (3.9) : construction progressive, une fois 3.3 fonctionnel et les mesures de base disponibles ; évaluation selon le critère de succès.
6. **Phase 4** (1.5, 3.5, 3.6) : multi-échelle et raffinement à 0,1 cent.
7. **Phase 5** (1.3, 1.4, 1.7, 2.1, 2.2, Lot 4) : B fin et Break, tests improveHighFrequencyPeaks et fenêtrage, interface du Break, audit de la pondération, validation complète sur de nouveaux scans.

## 7. Questions ouvertes

**En bref.** Ce qui reste à décider ou à vérifier.

- Code FFT partagé : paramètre par module ou drapeau global ?
- Séquences de l'Annexe A : choix de l'étape 6 de la méthode 1 (D#4 ou C#3) ; placement des octaves de la méthode 2 (G4 hors zone).
- Pianos dont le clavier ne contient pas toute la zone F3–F4 : quelle octave de tempérament utiliser, et quelles notes sauter (3.9) ?
- Fenêtre issue de la tolérance de l'ancien module suffisante pour l'octave centrale (réponse du journal) ?
- breakKeyIndex : valeur par défaut mNumberOfBassKeys ou détection depuis les données ?
- Format du journal : JSON ou CSV ?
- libuv : retirable ou non (résultat de `git grep -nE 'uv.h|uv_|libuv'`) ?

## 8. Historique des versions

- **V2.1** : scan complet, module séparé, indicateur H\_rec et H\_courbe, corrections FFT.
- **V2.2** : corrections de revue (copie du prétraitement, essais séparés, tolérance remise).
- **V2.3** : construction progressive, journalisation, audit de pondération à la place de l'égaliseur.
- **V2.4** : ancrage sur le tempérament théorique, Break à 4 touches, trois étapes de 3.9 avec fenêtre égale à la tolérance de l'ancien module, critère de succès, résumés « En bref », annexe des séquences.
- **V2.5** : nombre de touches variable (N, notes relatives à A4), contenu des .ept établi (C14) et plan d'essais sans WAV, arrondis de getPitchET440, précision XML ramenée à une phrase, test d'ouverture croisée des .ept, séquences complétées (A3 en méthode 1, F3 en méthode 2).

## Annexe A — Séquences d'accordage (ordre d'ajout des notes)

**En bref.** Trois ordres inspirés de méthodes manuelles, convertis de la notation française en notation scientifique, en numéros de touche (piano de 88 touches, A0 = 0) et en position relative à A4 (à utiliser pour un piano de N touches quelconque). Seul l'ordre est utilisé ; les intervalles et nombres de battements cités dans les sources sont ignorés. Origine : documents fournis par l'utilisateur, fidélité historique non vérifiée. Les trois méthodes ne diffèrent que dans l'octave de tempérament ; le report par octaves est commun. **À valider note par note avant implémentation.**

**Méthode 1 — « trois tierces »**

| Étape | Français | Scientifique | Touche | Rel. A4 |
| --- | --- | --- | --- | --- |
| 1 | La3 (référence) | A4 | 48 | 0 |
| 2 | Fa2 | F3 | 32 | −16 |
| 3 | Do♯3 | C#4 | 40 | −8 |
| 4 | Fa3 | F4 | 44 | −4 |
| 5 | Sol♯2 | G#3 | 35 | −13 |
| 6 | Ré♯3 (la source dit « Do♯2 ou Ré♯3 ») | D#4 (ou C#3) | 42 (ou 28) | −6 (ou −20) |
| 7 | Mi3 | E4 | 43 | −5 |
| 8 | Si2 | B3 | 38 | −10 |
| 9 | Fa♯2 | F#3 | 33 | −15 |
| 10 | Do3 | C4 | 39 | −9 |
| 11 | Sol2 | G3 | 34 | −14 |
| 12 | Ré3 | D4 | 41 | −7 |
| 13 | La♯2 | A#3 | 37 | −11 |
| 14 | La2 (ajout) | A3 | 36 | −12 |

L'étape 14 est ajoutée pour compléter la zone F3–F4 ; elle ne figure pas dans la liste source.

**Méthode 2 — quintes et quartes**

| Étape | Français | Scientifique | Touche | Rel. A4 |
| --- | --- | --- | --- | --- |
| 1 | La3 (référence) | A4 | 48 | 0 |
| 2 | Ré3 | D4 | 41 | −7 |
| 3 | Sol3 | G4 | 46 | −2 |
| 4 | Do3 | C4 | 39 | −9 |
| 5 | Fa3 | F4 | 44 | −4 |
| 6 | La♯2 | A#3 | 37 | −11 |
| 7 | Ré♯3 | D#4 | 42 | −6 |
| 8 | Sol♯2 | G#3 | 35 | −13 |
| 9 | Do♯3 | C#4 | 40 | −8 |
| 10 | Fa♯2 | F#3 | 33 | −15 |
| 11 | Si2 | B3 | 38 | −10 |
| 12 | Mi3 | E4 | 43 | −5 |
| 13 | Sol2 (ajout) | G3 | 34 | −14 |
| 14 | La2 (ajout) | A3 | 36 | −12 |
| 15 | Fa2 (ajout) | F3 | 32 | −16 |

Les étapes 13 à 15 sont ajoutées pour compléter la zone F3–F4. G4 (étape 3) est hors zone, comme dans la source.

**Méthode 3 — « Fa–Fa »**

| Étape | Français | Scientifique | Touche | Rel. A4 |
| --- | --- | --- | --- | --- |
| 1 | La3 (référence) | A4 | 48 | 0 |
| 2 | Fa2 | F3 | 32 | −16 |
| 3 | Fa3 | F4 | 44 | −4 |
| 4 | Do3 | C4 | 39 | −9 |
| 5 | Sol2 | G3 | 34 | −14 |
| 6 | Ré3 | D4 | 41 | −7 |
| 7 | La2 | A3 | 36 | −12 |
| 8 | Mi3 | E4 | 43 | −5 |
| 9 | Si2 | B3 | 38 | −10 |
| 10 | Fa♯2 | F#3 | 33 | −15 |
| 11 | Do♯3 | C#4 | 40 | −8 |
| 12 | Sol♯2 | G#3 | 35 | −13 |
| 13 | Ré♯3 | D#4 | 42 | −6 |
| 14 | La♯2 | A#3 | 37 | −11 |

Couvre les 13 notes de F3 à F4.
