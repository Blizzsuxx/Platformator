# Primer Diplomskog Rada Za Platformator

> Napomena: Ovaj dokument je primer sadržaja i strukture diplomskog rada prilagođen projektu Platformator. Tekst je namerno napisan tako da može da posluži kao početna verzija rada, ali i dalje sadrži mesta koja treba dopuniti stvarnim podacima o kandidatu, mentoru, datumima, konačnim merenjima i formalnim bibliografskim zapisima koje bude zahtevao mentor ili fakultetski šablon.

---

UNIVERZITET U NOVOM SADU  
FAKULTET TEHNIČKIH NAUKA U NOVOM SADU

<Ime i prezime kandidata>

RAZVOJ 2D GEJM ENDŽINA SA EDITOROM SCENA I OPTIMIZOVANOM DETEKCIJOM KOLIZIJA ZASNOVANOM NA SEGMENTISANOJ SWEEP-AND-PRUNE METODI

Diplomski rad  
- Osnovne akademske studije -

Novi Sad, 2026.

---

UNIVERZITET U NOVOM SADU  
FAKULTET TEHNIČKIH NAUKA  
21000 NOVI SAD, Trg Dositeja Obradovića 6

Datum: <datum>

ZADATAK ZA IZRADU DIPLOMSKOG (BACHELOR) RADA  
List: 1/1

Vrsta studija: Osnovne akademske studije  
Studijski program: Softversko inženjerstvo i informacione tehnologije  
Rukovodilac studijskog programa: prof. dr Miroslav Zarić

Student: <Ime i prezime>  
Broj indeksa: SW xx/20xx

Oblast: Softversko inženjerstvo / razvoj igara / računarska grafika  
Mentor: dr <Ime i prezime>, <zvanje>

NA OSNOVU PODNETE PRIJAVE, PRILOŽENE DOKUMENTACIJE I ODREDBI STATUTA FAKULTETA IZDAJE SE ZADATAK ZA DIPLOMSKI RAD, SA SLEDEĆIM ELEMENTIMA:

- problem – tema rada;
- način rešavanja problema i način praktične provere rezultata rada;
- literatura.

NASLOV DIPLOMSKOG (BACHELOR) RADA:  
Razvoj 2D gejm endžina sa editorom scena i optimizovanom detekcijom kolizija zasnovanom na segmentisanoj sweep-and-prune metodi

TEKST ZADATKA:

1. Analizirati stanje u oblasti 2D gejm endžina, editora scena i algoritama za broad-phase detekciju kolizija.
2. Izraditi specifikaciju zahteva softverskog rešenja koje obuhvata jezgro gejm endžina, sistem komponenti, fizički podsistem i desktop editor scena.
3. Izraditi specifikaciju dizajna softverskog rešenja sa posebnim osvrtom na podsistem za detekciju kolizija zasnovan na segmentisanoj sweep-and-prune metodi.
4. Implementirati softversko rešenje prema izrađenoj specifikaciji.
5. Testirati implementirano softversko rešenje kroz regresione testove, funkcionalne scenarije i demonstracione primere.
6. Dokumentovati stanje u oblasti, teorijsku osnovu, dizajn, implementaciju, evaluaciju i ograničenja razvijenog rešenja.

Rukovodilac studijskog programa: ____________________  
Mentor rada: ____________________

Primerak za: studenta i mentora.

---

## Sadržaj

1. Uvod
2. Pregled stanja u oblasti
3. Teorijski pojmovi i definicije
4. Metodologija i implementacija sistema
5. Evaluacija rešenja i rezultati
6. Demonstracija implementiranog sistema
7. Zaključak
8. Literatura
9. Biografija
10. Ključna dokumentacijska informacija
11. Key words documentation

---

## 1. Uvod

Gejm endžini predstavljaju ključnu softversku infrastrukturu za razvoj savremenih video igara i interaktivnih simulacija. Oni objedinjuju upravljanje objektima scene, renderovanje, audio reprodukciju, skriptovanje, serijalizaciju sadržaja i fizičku simulaciju u jedinstven okvir koji omogućava razvoj složenih aplikacija na višem nivou apstrakcije. Zbog toga su gejm endžini značajni ne samo u industriji zabave, već i u obrazovanju, prototipizaciji interaktivnih sistema, vizuelizaciji i simulacionim okruženjima.

U okviru ovog rada razmatra se problem razvoja sopstvenog 2D gejm endžina koji će omogućiti jednostavno formiranje scena, upravljanje komponentama, osnovnu fiziku i efikasnu detekciju kolizija za veći broj objekata. Poseban fokus rada je broad-phase detekcija kolizija, odnosno brzo pronalaženje parova objekata koji mogu biti u kontaktu, jer ta faza direktno utiče na performanse simulacije. Pored toga, cilj rada je da razvijeno rešenje ne ostane samo biblioteka koda, već da bude dopunjeno desktop editorom scena nalik na Unity pristup, tako da korisnik može vizuelno da uređuje objekte, komponente i resurse.

Problem je rešen razvojem sistema Platformator, implementiranog pre svega u jezgru na programskom jeziku C++20, uz oslanjanje na SDL3 za prozor, renderovanje i audio, Eigen za rad sa vektorima i matricama, oneTBB za odabrane paralelne delove obrade i nlohmann/json za serijalizaciju scena. Arhitektura sistema zasnovana je na komponentnom modelu, dok je fizički podsistem organizovan oko grid-subdivizije prostora, AABB struktura i segmentisanih intervalnih listi inspirisanih radom Tracy, Buss i Woods [1]. Vizuelni editor scena razvijen je kao zasebna Python/PySide6 aplikacija koja omogućava pregled hijerarhije, inspektor komponenti, pregled resursa i pokretanje projekta iz samog alata.

Evaluacija rešenja sprovedena je kombinacijom automatizovanih testova, ručno vođenih funkcionalnih scenarija i demonstracionog primera igre. Automatizovana verifikacija obuhvata regresione testove za stabilnost fizičkog sistema, broad-phase logiku, serijalizaciju scena, audio i animator komponente, kao i testove bezbednog brisanja objekata i rekreacije runtime okruženja. Pored toga, kao demonstracija upotrebljivosti razvijen je Mario primer koji prikazuje da endžin može da posluži kao baza za stvarni 2D platformer. Rezultati pokazuju da implementacija uspešno integriše istraživačku ideju optimizovane broad-phase kolizione detekcije sa praktičnim alatima potrebnim za svakodnevni razvoj sadržaja. Istovremeno, analiza ukazuje na preostala ograničenja sistema, kao što su odsustvo kontinuirane detekcije kolizija za veoma brze objekte i potreba za dodatnom kvantitativnom evaluacijom performansi u velikim sintetičkim scenama.

Ostatak rada organizovan je na sledeći način. U drugom poglavlju dat je pregled stanja u oblasti, sa fokusom na broad-phase algoritme, gejm endžine i editore scena. Treće poglavlje uvodi teorijske pojmove potrebne za razumevanje rešenja, uključujući komponentnu arhitekturu, sweep-and-prune pristup, segmentisane intervalne liste, SAT test i impulsno rešavanje kontakata. U četvrtom poglavlju opisana je metodologija i implementacija sistema Platformator. Peto poglavlje sadrži evaluaciju rešenja i rezultate. U šestom poglavlju prikazana je demonstracija sistema kroz editor i Mario primer. Na kraju rada dati su zaključci, pravci budućeg razvoja, literatura i obavezni dokumentacioni dodaci.

---

## 2. Pregled stanja u oblasti

U ovom poglavlju predstavljena su rešenja i radovi relevantni za temu razvoja gejm endžina sa akcentom na detekciju kolizija, arhitekturu sistema i alate za uređivanje scena. Prilikom izbora srodnih radova i sistema primenjena su tri kriterijuma. Prvi kriterijum je sličnost u samom problemu broad-phase detekcije kolizija. Drugi kriterijum je sličnost u arhitekturi, odnosno upotreba komponentnog modela za organizaciju gejm objekata. Treći kriterijum je prisustvo alata za uređivanje scena koji olakšavaju rad krajnjem korisniku i približavaju rešenje profesionalnim alatima kakvi su Unity ili Godot.

### 2.1. Algoritmi za detekciju kolizija i broad-phase pristupi

Jedan od klasičnih radova u oblasti fizičke simulacije i sudarne detekcije je rad Baraffa [2], u kome je detaljno razmatrana simulacija krutih tela bez prodiranja. Iako taj rad ne definiše direktno savremenu arhitekturu gejm endžina, on predstavlja važnu teorijsku osnovu za kasnije broad-phase i narrow-phase pristupe. U ranim implementacijama broad-phase detekcije često su korišćeni prostorni hijerarhijski modeli ili jednostavno pretraživanje svih parova objekata, ali je njihov problem bio slaba skalabilnost kada broj objekata raste.

Značajan napredak ostvaren je razvojem sweep-and-prune pristupa, u kome se AABB projekcije objekata sortiraju po osama i koriste za brzo utvrđivanje kandidata za sudar [2]. Dalje unapređenje vidljivo je u sistemu I-COLLIDE [3], gde autori koriste inkrementalno održavanje sortiranih projekcija kako bi iskoristili vremensku koherentnost scene. Prednost ove ideje je to što objekti između susednih frejmova često menjaju položaj relativno malo, pa nije potrebno iznova vršiti skupu globalnu obradu svih projekcija.

Rad Tracy, Buss i Woods [1] je posebno značajan za ovaj diplomski rad, jer uvodi segmentisane intervalne liste kao način da se insertion i removal događaji obrade efikasnije nego u tradicionalnom sweep-and-prune modelu. Umesto jedne velike liste projekcija, autori predlažu strukturu sastavljenu od povezanih malih sortiranih nizova uz dodatne checkpoint skupove koji čuvaju lokalnu informaciju o intervalima koji prelaze granice segmenata. Takav pristup je naročito koristan u velikim, vremenski koherentnim scenama sa mnogo objekata u mirovanju i relativno malim brojem dinamičkih promena [1]. Upravo ta karakteristika ga čini pogodnim za 2D platformske igre i simulacione scene sa velikim brojem statičkih prepreka i manjim brojem pokretnih objekata.

Pored broad-phase algoritama, važan deo sistema čini i narrow-phase detekcija. U praktičnim sistemima za 2D kolizije često se koristi SAT, odnosno Separating Axis Theorem, koji omogućava efikasan test preseka konveksnih oblika na osnovu projekcija na konačan skup osa [4]. Prednost SAT pristupa je u tome što, pored samog odgovora da li dva objekta kolidiraju, može da pruži i informaciju o normalama kontakta i dubini penetracije, što je direktno korisno za fizički odgovor.

### 2.2. Gejm endžini i editori scena

Savremeni gejm endžini kao što su Unity i Godot popularizovali su pristup u kome je razvoj igre organizovan oko scene, hijerarhije objekata, komponenti i inspektora svojstava. U Unity okruženju korisnik scene formira kombinovanjem GameObject entiteta i pridruženih komponenti, dok se uređivanje vrši kroz vizuelni editor koji objedinjuje hijerarhijski pregled, inspector, asset browser i pokretanje projekta iz istog okruženja [5]. Sličan pristup nudi i Godot, koji stavlja poseban akcenat na organizaciju scene kao stabla čvorova i na integrisani editor [6].

Za akademski i inženjerski rad važno je uočiti da komercijalni endžini rešavaju veliki broj problema odjednom: renderovanje, audio, fizičku simulaciju, uvoz resursa, uređivanje scena i izvođenje skripti. Međutim, takvi sistemi su istovremeno veoma složeni i zatvoreni za detaljno proučavanje pojedinačnih algoritamskih odluka. Zato razvoj sopstvenog, manjeg endžina predstavlja pogodan okvir za obrazovni i istraživački rad: omogućava studentu da razume unutrašnju strukturu sistema, implementira i testira konkretne algoritme i da zatim te odluke uporedi sa postojećim industrijskim praksama.

U kontekstu ovog rada, editor scena je važan zato što podiže rad iznad nivoa same biblioteke. Ako krajnji korisnik može da kreira scenu, doda objekte i komponente, poveže resurse i odmah pokrene izvršavanje, tada sistem postaje upotrebljiv i kao razvojna platforma. Zbog toga su Unity-like karakteristike, kao što su hijerarhija objekata, inspektor svojstava, pregled resursa i integracija sa build/run procesom, relevantne i za evaluaciju kvaliteta razvijenog rešenja, a ne samo za njegovu prezentaciju.

### 2.3. Rešenja najbliža ovom radu

Po pitanju broad-phase algoritma, rešenje najbliže ovom radu je upravo rad Tracy, Buss i Woods [1]. Sličnost se ogleda u tome što i Platformator koristi grid-subdiviziju, AABB projekcije i segmentisane intervalne liste za održavanje kandidata za koliziju. Razlika je u tome što je originalni rad koncipiran kao opšti pristup za velike virtuelne scene, dok je Platformator primenjen u okviru 2D gejm endžina sa komponentnom arhitekturom, sistemom scena, skriptovanjem i desktop editorom. Drugim rečima, ovaj rad ne preuzima samo algoritam, već ga ugrađuje u širi softverski proizvod.

Sa aspekta arhitekture i korisničkog iskustva, najbliža rešenja su Unity i Godot [5], [6]. Iako Platformator ne teži da po obimu i stepenu zrelosti konkuriše tim sistemima, od njih preuzima organizacione ideje koje su se pokazale uspešnim u praksi: scene kao osnovnu jedinicu sadržaja, komponentno modelovanje ponašanja i vizuelni editor za rad sa objektima i resursima. U tom smislu, doprinos rada nije u tome da zameni industrijski alat, već da prikaže kako se kombinacijom istraživačkog algoritma i inženjerske arhitekture može formirati koherentno razvojno okruženje za 2D igre.

Na osnovu pregleda stanja u oblasti može se zaključiti da postoje dva glavna pravca na koja se ovaj rad oslanja. Prvi je algoritamski pravac, oličen u radovima o broad-phase detekciji kolizija i SAT obradi kontakata. Drugi je inženjersko-alatni pravac, oličen u savremenim gejm endžinima sa integrisanim editorima. Upravo kombinacija ta dva pravca predstavlja osnovnu motivaciju i glavnu posebnost sistema Platformator.

---

## 3. Teorijski pojmovi i definicije

U ovom poglavlju predstavljeni su teorijski koncepti neophodni za razumevanje implementacije sistema Platformator. Najpre je razmotrena komponentna arhitektura gejm endžina, zatim broad-phase detekcija kolizija zasnovana na sweep-and-prune pristupu i segmentisanim intervalnim listama, a na kraju i narrow-phase obrada kontakata uz primenu SAT pristupa i impulsnog rešavanja kolizija.

### 3.1. Komponentna arhitektura gejm endžina

Komponentna arhitektura polazi od ideje da objekat scene ne treba modelovati dubokim nasleđivanjem klasa, već kompozicijom manjih, jasno definisanih funkcionalnih delova. U takvom modelu GameObject predstavlja identitet i transformaciju objekta, dok se konkretne sposobnosti dodaju kroz komponente, kao što su Sprite, Collider, Rigidbody, Audio, Camera ili ScriptComponent. Prednost ovakvog pristupa je u visokoj fleksibilnosti: isti objekat može u jednoj sceni biti samo vizuelni entitet, a u drugoj i fizičko telo, audio izvor i nosilac skripti.

U sistemima inspirisanim Unity pristupom komponenta je obično povezana sa jednim objektom scene, dok editor omogućava dodavanje i uređivanje komponenti bez ručnog menjanja koda za osnovnu strukturu objekta. Time se postiže razdvajanje odgovornosti između sistema koji upravljaju renderovanjem, fizikom, audio reprodukcijom i ponašanjem. U okviru ovog rada takav model je važan ne samo zbog preglednosti implementacije, već i zato što olakšava vezivanje fizičkog podsistema sa sistemom scena i editorom.

### 3.2. Sweep-and-prune i segmentisane intervalne liste

Sweep-and-prune pristup broad-phase detekciji kolizija zasniva se na projekciji AABB granica objekata na jednu ili više koordinatnih osa. Ako se projekcije dva objekta ne preklapaju ni na jednoj osi, tada je sigurno da se objekti ne seku. Ako se preklapaju na svim posmatranim osama, par se prosleđuje u narrow-phase obradu. Ključna prednost ove metode je to što se veliki broj potencijalnih parova može odbaciti veoma rano, pre nego što se primene skuplji geometrijski testovi.

U klasičnoj verziji algoritma održava se sortirana lista početaka i krajeva intervala. Kada se minimum nekog intervala pojavi pre maksimuma drugog intervala, između odgovarajućih objekata postoji preklapanje na toj osi. Problem tradicionalnog pristupa nastaje kada je potrebno efikasno obraditi dodavanje, uklanjanje ili veće pomeranje objekata bez ponovnog globalnog sortiranja velikog niza projekcija.

Segmentisane intervalne liste rešavaju taj problem podelom velike liste na manje lokalno sortirane celine, odnosno segmente ili chunk-ove [1]. Svaki segment čuva manji niz projekcija, a dodatni checkpoint skup održava informaciju o intervalima koji prelaze granice segmenta. Zahvaljujući tome, umetanje i uklanjanje intervala može da se obavi lokalno, uz znatno manji broj pomeranja elemenata nego kod monolitne strukture. U okruženjima sa velikim brojem uglavnom statičkih objekata i manjim brojem lokalnih promena, to vodi ka povoljnijim performansama od jednostavnijih pristupa [1].

U ovom radu taj teorijski model je dodatno povezan sa prostornom subdivizijom scene u grid ćelije. Time se smanjuje gustina projekcija po pojedinačnoj strukturi, jer svaka ćelija održava sopstvene lokalne AABB parove. Takva kombinacija spaja ideju spatial subdivision pristupa i ideju inkrementalnog održavanja intervalnih listi.

### 3.3. SAT i impulsno rešavanje kolizija

Nakon broad-phase filtriranja, kandidat-parovi se proveravaju u narrow-phase fazi. Za 2D konveksne oblike, poput pravougaonika i krugova, praktičan izbor predstavlja SAT. Teorema o razdvajajućoj osi kaže da se dva konveksna oblika ne seku ako postoji bar jedna osa na kojoj se njihove projekcije ne preklapaju [4]. U slučaju da takva osa ne postoji, oblici se seku. Pored same detekcije, minimum preklapanja po svim posmatranim osama daje korisnu aproksimaciju normale i dubine penetracije.

Fizički odgovor na koliziju u ovom radu zasniva se na impulsnom rešavanju kontakata. Umesto rešavanja kompletne dinamike u jednom velikom sistemu jednačina, kontaktne tačke se iterativno koriguju kroz normalne i tangentne impulse. Ovakav pristup je rasprostranjen u real-time fizičkim sistemima zato što pruža dobar kompromis između stabilnosti, performansi i složenosti implementacije. U okviru praktičnog gejm endžina takav izbor omogućava dovoljno uverljivo ponašanje rigidbody objekata, a da sistem ostane dovoljno brz za izvođenje u svakom frejmu.

---

## 4. Metodologija i implementacija sistema

U ovom poglavlju predstavljena je implementacija sistema Platformator. Ulaz u sistem čine definicije scena, resursi projekta i korisničke akcije u editoru ili samoj igri. Očekivani izlaz sistema su izvršive 2D scene sa renderovanim objektima, odigranim audio sadržajem, obrađenim skriptama i stabilnom fizičkom simulacijom kolizija. Sistem je podeljen na više međusobno povezanih modula: jezgro runtime okruženja, fizički podsistem, editor scena i demonstracioni primer igre.

Na visokom nivou apstrakcije arhitektura može se prikazati sledećim tokom podataka:

```text
Editor scena / Mario primer
        |
        v
   JSON scena + assets
        |
        v
  Runtime / GameManager
        |
        +--> Renderer + Audio
        |
        +--> Script sistem
        |
        +--> PhysicsManager
                |
                +--> Grid
                +--> AABB po ćeliji
                +--> SegmentedIntervalList
                +--> SAT + kontaktne tačke
                +--> impulsno rešavanje
```

### 4.1. Jezgro sistema i komponentni model

Centralna izvršna celina sistema je runtime, koji instancira GameManager i preko njega upravlja scenom, objektima, prozorom i pomoćnim podsistemima. GameObject predstavlja osnovni entitet scene i sadrži transformaciju, identitet, roditeljsko-dete relacije i skup pridruženih komponenti. Na ovaj način je omogućeno da se ponašanje objekata gradi kompozicijom, a ne kreiranjem velikog broja specijalizovanih klasa.

Implementirane komponente obuhvataju vizuelne, fizičke i logičke aspekte scene. Sprite komponenta zadužena je za prikaz tekstura i odabir izvornog pravougaonika unutar teksture. Collider komponente modeluju geometriju objekta relevantnu za kolizije, dok Rigidbody uvodi masu, brzinu, sile i tip tela. Animator upravlja smenom frejmova animacije, Audio omogućava reprodukciju zvuka, a ScriptComponent je namenjen vezivanju korisničkih ponašanja uz objekte scene.

Ovako organizovan sistem je prilagođen i radu kroz editor. Korisnik ne mora da konstruiše objekte direktno u kodu, već može da ih dodaje kroz interfejs, čuva u JSON scene datotekama i zatim pokreće unutar runtime okruženja. Time se postiže tok rada sličan modernim gejm endžinima.

### 4.2. Fizički podsistem i detekcija kolizija

Fizički podsistem organizovan je oko PhysicsManager komponente koja upravlja rigidbody objektima, kolajderima, pending sinhronizacijama i aktivnim sudarima. Pri promeni položaja ili geometrije kolajdera vrši se osvežavanje AABB projekcija, kao i sinhronizacija grid ćelija koje objekat pokriva. Time se broad-phase logika održava inkrementalno, bez potrebe za potpunom rekonstrukcijom svih struktura pri svakom frejmu.

Prostor scene deli se na grid ćelije. Svaka ćelija poseduje sopstvenu AABB strukturu sa X i Y intervalnim listama. Kandidat-parovi koji se preklapaju u svim osama unutar iste ćelije predstavljaju lokalne svedoke preklapanja. Na nivou celog sistema koristi se witness counting mehanizam kojim se evidentira da li se par objekata preklapa u najmanje jednoj ćeliji. Na taj način sistem korektno obrađuje objekte koji obuhvataju više ćelija istovremeno.

Za održavanje lokalnog redosleda projekcija koriste se segmentisane intervalne liste. Svaki segment čuva mali sortirani niz projekcija i checkpoint skup. Pri dodavanju ili uklanjanju intervala sistem najpre locira odgovarajući segment, zatim lokalno ažurira njegov sadržaj, a u slučajevima prelaženja granice segmenta ažurira checkpoint informaciju i eventualno deli ili spaja segmente. Ovakva organizacija je pogodna za 2D platformske scene u kojima postoji veliki broj statičkih platformi, zidova i dekorativnih objekata, dok se relativno mali broj objekata aktivno kreće kroz prostor.

Nakon broad-phase faze, kandidat-parovi prolaze kroz SAT proveru. Za pravougaonike se koriste ose definisane njihovim normalama, dok se za krugove i mešovite parove koriste odgovarajuće osi i pomoćni geometrijski proračuni. Iz SAT faze dobijaju se normalni pravac kontakta, penetracija i tačke kontakta, nakon čega impulsni solver iterativno koriguje brzine i ugaone brzine objekata. Dodatno su implementirane podrške za trenje, restituciju, kinematička tela i mehanizam uspavljivanja rigidbody objekata koji stabilno počivaju na podlozi.

### 4.3. Editor scena nalik na Unity interfejs

Pored jezgra endžina, razvijen je i desktop editor scena koji funkcioniše kao zaseban alat. Editor je implementiran u Python okruženju uz PySide6 i pruža više panela koji prate logiku savremenih razvojnih okruženja za igre. Korisniku su na raspolaganju panel hijerarhije objekata, inspektor svojstava, pregled biblioteke ponašanja, pregled resursa i izlazni panel sa porukama build i run procesa.

Scene se u editoru predstavljaju modelima koji odražavaju JSON format koji koristi runtime. To omogućava round-trip scenario u kome korisnik otvori postojeću scenu, izvrši izmene i ponovo je sačuva bez gubitka poznatih podataka. Posebna pažnja posvećena je normalizaciji putanja ka resursima tako da se one čuvaju u stabilnom obliku zasnovanom na `assets/...` pravilima.

Unity-like karakter ovog editora ogleda se pre svega u organizaciji rada. Objekti su prikazani hijerarhijski, komponente se uređuju kroz inspector, resursi su izdvojeni u zaseban prikaz, a build i pokretanje scene mogu se obaviti iz samog alata. Na taj način se dobija okruženje pogodno ne samo za demonstraciju endžina, već i za praktičan razvoj manjih projekata.

### 4.4. Mario primer kao demonstracija potrošača biblioteke

Da bi se pokazalo da Platformator nije vezan samo za interni testni kod, razvijen je Mario primer kao zaseban potrošač biblioteke. Primer koristi javni runtime interfejs endžina, sopstvene C++ skripte za ponašanje igrača, kamera rig, patrolirajuće neprijatelje, novčiće i ciljnu zastavu, kao i posebnu scenu i resurse koji se kopiraju u runtime direktorijum pri izgradnji.

Mario primer ima dvostruku ulogu u radu. Sa jedne strane, on predstavlja demonstraciju da je endžin dovoljno kompletan da podrži jednostavnu platformsku igru sa skriptama, kolizijama, animacijama i audio sadržajem. Sa druge strane, on služi kao scenario za proveru integracije između scene, runtime-a, komponentnog sistema i fizičkog podsistema. Upravo zbog toga ovaj primer treba posmatrati kao deo validacije upotrebljivosti, a ne samo kao vizuelni dodatak projektu.

### 4.5. Korišćeni alati

Jezgro endžina razvijeno je u programskom jeziku C++20. Za rad sa prozorom, događajima, renderovanjem i audio sadržajem korišćen je SDL3 i prateće biblioteke SDL3_image, SDL3_ttf i SDL3_mixer [7]. Matematički proračuni zasnivaju se na biblioteci Eigen. Za odabrane paralelne delove obrade koristi se oneTBB. Format scena zasnovan je na JSON-u uz biblioteku nlohmann/json. Sistem za izgradnju projekta zasniva se na CMake-u.

Editor scena razvijen je u Python-u uz PySide6. Ovakav izbor je opravdan time što desktop alat zahteva brzu iteraciju nad korisničkim interfejsom i jednostavno modelovanje formi, panela i integracije sa eksternim procesima za build i run operacije. Kombinacija C++ jezgra i Python editora predstavlja praktičan kompromis između performansi izvršnog dela sistema i brzine razvoja alata.

---

## 5. Evaluacija rešenja i rezultati

U ovom poglavlju prikazan je način na koji je verifikovano da sistem Platformator ispunjava osnovne funkcionalne i arhitekturalne zahteve. Za razliku od radova iz oblasti mašinskog učenja, ovde nije reč o klasičnom skupu podataka za treniranje i testiranje modela, već o skupu test scenarija, automatizovanih regresionih provera i demonstracionih scena. Zbog toga je evaluacija organizovana tako da obuhvati korektnost implementacije, stabilnost rada i praktičnu upotrebljivost.

### 5.1. Skup test scenarija

Test scenariji korišćeni u ovom radu mogu se podeliti u tri grupe.

Prvu grupu čine sintetički fizički scenariji kojima se proverava ispravnost detekcije i rešavanja kolizija. Tu spadaju scenariji stabilnosti sudara krugova, ponovne upotrebe kontaktnih tačaka, ponašanja pri restituiciji, kinematičkih tela, podrške za uspavljivanje rigidbody objekata, kao i scenariji koji ciljano proveravaju checkpoint logiku, kretanje preko granica chunk-ova i spajanje nedovoljno popunjenih segmenata.

Drugu grupu čine scenariji serijalizacije i rada sa resursima. U ovoj grupi se proverava da li učitavanje i snimanje scena čuva ključne informacije o objektima i komponentama, da li su putanje ka resursima kanonizovane i da li runtime može bezbedno da rekreira okruženje nakon prethodnog gašenja.

Treću grupu čine integracioni i demonstracioni scenariji. Oni obuhvataju Mario primer, ručno testiranje editora scena, otvaranje i čuvanje scena, dodavanje objekata i komponenti, kao i build i pokretanje scene iz samog UI alata.

### 5.2. Postupak verifikacije

Automatizovana verifikacija sprovedena je kroz više izvršnih test programa. Najvažniji među njima je `platformator_regression_tests`, koji sadrži regresione scenarije za fiziku, broad-phase, serijalizaciju, audio, animator i rad sa hijerarhijom objekata. Dodatno postoje posebni testovi za round-trip skriptovanih scena, bezbedno brisanje objekata koji su u redu čekanja za obradu, kao i za ponovno kreiranje runtime okruženja u istom procesu.

U okviru funkcionalne verifikacije izvršeni su i ručni scenariji upotrebe editora. Ti scenariji obuhvataju kreiranje nove scene, otvaranje postojeće scene, izmenu položaja i komponenti objekata, pregled biblioteke ponašanja, čuvanje scene i pokretanje scene iz editora. Time se ne proverava samo ispravnost samog UI-a, već i stabilnost interfejsa između editora i runtime sistema.

Za kvantitativnu procenu performansi broad-phase algoritma predviđen je i dodatni benchmark postupak koji u završnoj verziji rada treba sprovesti nad sintetičkim scenama sa različitim brojem objekata, različitim procentom pokretnih objekata i različitim brojem insertion/removal događaja. U ovom primeru rada zadržan je format takve evaluacije, dok konkretne numeričke vrednosti treba popuniti nakon ciljano sprovedenih merenja.

### 5.3. Rezultati i diskusija

Tabela 1 prikazuje rezultat automatizovane funkcionalne verifikacije nad projektno definisanim test izvršnim programima.

| Test izvršni program | Svrha | Rezultat |
| --- | --- | --- |
| `platformator_regression_tests` | Regresiona provera fizike, broad-phase logike, serijalizacije, animacije, audio i rada sa objektima | Prošao |
| `platformator_scene_script_roundtrip_test` | Provera serijalizacije scena i skripti | Prošao |
| `platformator_queued_deletion_safety_test` | Provera bezbednog brisanja objekata tokom obrade | Prošao |
| `platformator_runtime_recreation_test` | Provera sekvencijalnog kreiranja i gašenja runtime okruženja | Prošao |

Pored zbirnog rezultata iz Tabele 1, bitno je istaći da regresioni test program pokriva više scenarija koji obuhvataju stabilnost kolizionog sistema, checkpoint logiku segmentisanih intervalnih listi, spajanje chunk-ova, round-trip animacionih i audio podataka, kao i rad sa parent-child hijerarhijom objekata. Ovakva širina pokrivenosti je važna zato što pokazuje da sistem nije validiran samo kroz jedan demonstracioni primer, već i kroz skup ciljanih tehničkih provera.

Kvalitativna evaluacija editora pokazala je da je osnovni tok rada funkcionalan: korisnik može da učita ili kreira scenu, menja strukturu objekata i komponenti, pregleda resurse i pokrene build ili izvršavanje scene bez izlaska iz alata. U tom smislu, editor ispunjava primarni cilj rada, a to je da Platformator ne bude samo eksperimentalni fizički podsistem, već upotrebljivo razvojno okruženje za 2D scene.

Tabela 2 predstavlja primer formata za kvantitativnu procenu performansi broad-phase algoritma. Ovu tabelu u završnoj verziji rada treba popuniti stvarnim merenjima.

| Scenario | Broj kolajdera | Udeo pokretnih objekata | Insercije/brisanja po frejmu | Prosečno vreme broad-phase obrade [ms] | Prosečno vreme frejma [ms] |
| --- | --- | --- | --- | --- | --- |
| Mirna sintetička scena | [uneti] | [uneti] | [uneti] | [uneti] | [uneti] |
| Umereno aktivna sintetička scena | [uneti] | [uneti] | [uneti] | [uneti] | [uneti] |
| Gusta scena sa više ćelija | [uneti] | [uneti] | [uneti] | [uneti] | [uneti] |
| Mario primer | [uneti] | [uneti] | [uneti] | [uneti] | [uneti] |

Na osnovu trenutnog stanja sistema može se zaključiti da je funkcionalna korektnost implementacije dobro pokrivena, dok je kvantitativna analiza performansi još uvek prostor za dalje unapređenje rada. Drugim rečima, rad već sada demonstrira uspešnu implementaciju i integraciju podsistema, ali završna verzija diplomskog rada treba da dopuni taj rezultat detaljnim benchmark merenjima kako bi doprinos u pogledu performansi bio još ubedljivije argumentovan.

U diskusiji rezultata važno je biti realan i prema ograničenjima sistema. Trenutna verzija ne obuhvata batch insertion/removal putanju opisanu u radu [1], niti kontinuiranu detekciju kolizija za veoma brze objekte. Takođe, uočen je prostor za unapređenje redosleda fizičke obrade i dodatnu izolaciju projektnih testova od testova eksternih zavisnosti. Ove stavke ne umanjuju doprinos rada, ali jasno definišu pravce njegovog daljeg razvoja.

---

## 6. Demonstracija implementiranog sistema

Demonstracija sistema može se organizovati kao praktična prezentacija tri nivoa rada Platformator okruženja.

Prvi nivo demonstracije odnosi se na editor scena. U okviru odbrane rada kandidat može da pokaže pokretanje desktop editora, učitavanje scene, izbor objekta u hijerarhiji, izmenu komponenti u inspector panelu, čuvanje scene i pokretanje scene iz samog editora. Time se ilustruje da alat podržava osnovni razvojni tok koji korisnik očekuje od modernog gejm alata.

Drugi nivo demonstracije odnosi se na runtime izvršavanje scene. Ovde se može prikazati jednostavna test scena sa statičkim platformama, jednim dinamičkim telom, audio izvorom i animator komponentom. Posmatrač na taj način direktno vidi da su scene učitane, da se kolizije obrađuju, da animacija funkcioniše i da audio sistem pravilno reaguje na događaje.

Treći nivo demonstracije predstavlja Mario primer. Ovaj primer je posebno pogodan za odbranu rada zato što istovremeno demonstrira skriptovani karakter sistema, kolizije, rad kamere, logiku platformera, resurse i scene. Kroz njega se može pokazati da Platformator nije samo skup tehničkih modula, već koherentna platforma na kojoj je moguće realizovati konkretnu 2D igru.

U praksi, demonstracija bi mogla teći sledećim redosledom:

1. Otvaranje editora i prikaz panela Hijerarhija, Inspector, Behaviors, Assets i Output.
2. Učitavanje scene i kratka izmena jedne platforme ili objekta.
3. Pokretanje scene iz editora.
4. Pokretanje Mario primera i prikaz interakcije igrača sa okruženjem.
5. Kratak osvrt na testove i ograničenja sistema.

Ovakva demonstracija je važna zato što komisiji omogućava da vidi i alatni i izvršni deo rada, što je u ovom slučaju jedna od ključnih vrednosti projekta.

---

## 7. Zaključak

U ovom radu predstavljen je razvoj 2D gejm endžina Platformator sa pratećim editorom scena i fizičkim podsistemom koji se oslanja na segmentisanu sweep-and-prune broad-phase detekciju kolizija. Motivacija rada bila je da se istraživačke ideje iz oblasti optimizovane kolizione detekcije pretoče u konkretno, praktično upotrebljivo softversko rešenje koje objedinjuje scene, komponente, fiziku, resurse i alat za uređivanje sadržaja.

Na arhitekturalnom nivou sistem je realizovan kao kombinacija C++20 runtime jezgra i Python/PySide6 editora. Runtime pruža komponentni model, upravljanje scenama, fizički podsistem, renderovanje, audio i skriptovanje, dok editor omogućava vizuelni rad sa scenama i integraciju sa build/run procesom. Poseban doprinos rada ogleda se u integraciji grid-subdivizije, AABB parova, segmentisanih intervalnih listi i checkpoint mehanizma u okviru šireg gejm endžin okruženja.

Evaluacija rešenja pokazala je da projektni test izvršni programi uspešno prolaze funkcionalne provere, kao i da je sistem dovoljno kompletan da podrži demonstracioni platformer u vidu Mario primera. Time je potvrđeno da je razvijeno rešenje više od izolovane implementacije pojedinačnog algoritma. Ono predstavlja celovit prototip 2D gejm endžina sa pratećim alatima za kreiranje sadržaja.

Istovremeno, rad je identifikovao više pravaca daljeg razvoja. Pre svega, poželjno je sprovesti detaljniju kvantitativnu analizu broad-phase performansi nad sintetičkim scenama velikog obima. Dalje, kontinuirana detekcija kolizija za veoma brze objekte i batch insertion/removal putanja predstavljaju logične algoritamske nadogradnje. Takođe, postoji prostor za dodatno unapređenje redosleda fizičke obrade, paralelizacije i organizacije testnog okruženja. Upravo ti pravci mogu da posluže kao osnova za budući istraživački ili inženjerski rad.

Može se zaključiti da Platformator uspešno demonstrira kako se teorijski koncepti iz oblasti sudarne detekcije i arhitekture gejm endžina mogu objediniti u koherentno softversko rešenje. To ga čini pogodnim i kao predmet diplomskog rada i kao dalju osnovu za razvoj složenijih interaktivnih sistema.

---

## 8. Literatura

Napomena: Spisak literature u završnoj verziji rada uskladiti sa stilom citiranja koji odredi mentor ili fakultet.

[1] Tracy, D. J., Buss, S. R., Woods, B. M. Efficient Large-Scale Sweep and Prune Methods with AABB Insertion and Removal. Proceedings of the IEEE Virtual Reality Conference, 2009.

[2] Baraff, D. Dynamic Simulation of Non-Penetrating Rigid Bodies. PhD thesis, Cornell University, 1992.

[3] Cohen, J. D., Lin, M. C., Manocha, D., Ponamgi, M. K. I-COLLIDE: An Interactive and Exact Collision Detection System for Large-Scale Environments. Symposium on Interactive 3D Graphics, 1995.

[4] van den Bergen, G. Collision Detection in Interactive 3D Environments. Morgan Kaufmann, 2003.

[5] Unity Technologies. Unity Manual. Dostupno na: https://docs.unity3d.com/Manual/.

[6] Godot Engine. Official Documentation. Dostupno na: https://docs.godotengine.org/.

[7] SDL. SDL3 Documentation and Wiki. Dostupno na: https://wiki.libsdl.org/SDL3.

[8] Catto, E. Box2D Manual. Dostupno na: https://box2d.org/documentation/.

[9] Reppy, J. H., Appel, A. W., Shao, Z. Unrolling Lists. Proceedings of the 1994 ACM Conference on Lisp and Functional Programming, 1994.

[10] Platformator source code repository. Interna projektna dokumentacija i implementacija sistema. [navesti URL repozitorijuma ako je primenljivo].

---

## 9. Biografija

<Ime i prezime kandidata> je rođen/a <dd.mm.gggg.> godine u <mesto>. Osnovno i srednje obrazovanje završio/la je u <mesto>. Školske <20xx/20xx> godine upisuje osnovne akademske studije na Fakultetu tehničkih nauka u Novom Sadu, studijski program Softversko inženjerstvo i informacione tehnologije. Položio/la je sve ispite predviđene nastavnim planom i programom i stekao/la uslov za odbranu diplomskog rada.

---

## 10. Ključna dokumentacijska informacija

Redni broj, RBR: <uneti>  
Identifikacioni broj, IBR: <uneti>  
Tip dokumentacije, TD: monografska publikacija  
Tip zapisa, TZ: tekstualni štampani dokument  
Vrsta rada, VR: diplomski rad  
Autor, AU: <Ime i prezime kandidata>  
Mentor, MN: dr <Ime i prezime mentora>, <zvanje>  
Naslov rada, NR: Razvoj 2D gejm endžina sa editorom scena i optimizovanom detekcijom kolizija zasnovanom na segmentisanoj sweep-and-prune metodi  
Jezik publikacije, JP: srpski  
Jezik izvoda, JI: srpski / engleski  
Zemlja publikovanja, ZP: Srbija  
Uže geografsko područje, UGP: Vojvodina  
Godina, GO: 2026  
Izdavač, IZ: autorski reprint  
Mesto i adresa, MA: Novi Sad, Fakultet tehničkih nauka, Trg Dositeja Obradovića 6  
Fizički opis rada, FO: <broj poglavlja / stranica / citata / tabela / slika / grafikona / priloga>  
Naučna oblast, NO: Softversko inženjerstvo i informacione tehnologije  
Naučna disciplina, ND: Razvoj softverskih sistema / razvoj igara / računarska grafika  
Predmetna odrednica / ključne reči, PO: gejm endžin, detekcija kolizija, sweep and prune, editor scena, komponentna arhitektura  
UDK: <uneti>  
Čuva se, ČU: Biblioteka Fakulteta tehničkih nauka, Trg Dositeja Obradovića 6, Novi Sad  
Važna napomena, VN: <uneti ako postoji>  
Izvod, IZ:

U radu je predstavljen razvoj 2D gejm endžina Platformator sa desktop editorom scena i fizičkim podsistemom za detekciju i rešavanje kolizija. Jezgro sistema zasnovano je na komponentnoj arhitekturi i implementirano u programskom jeziku C++20 uz oslanjanje na SDL3, Eigen, oneTBB i JSON serijalizaciju scena. Poseban fokus rada je broad-phase detekcija kolizija zasnovana na grid-subdiviziji prostora, AABB strukturama i segmentisanim intervalnim listama inspirisanim radom Tracy, Buss i Woods. Pored runtime dela sistema razvijen je i editor scena nalik na Unity pristup, koji omogućava upravljanje hijerarhijom objekata, komponentama, resursima i pokretanjem scena. Funkcionalnost sistema proverena je automatizovanim testovima i demonstracionim Mario primerom. Rezultati pokazuju da sistem uspešno objedinjuje istraživačku ideju optimizovane kolizione detekcije i praktično razvojno okruženje za 2D igre, uz jasno identifikovane pravce za buduća unapređenja.

Datum prihvatanja teme, DP: <uneti>  
Datum odbrane, DO: <uneti>  
Članovi komisije, KO:  
predsednik: dr <Ime Prezime>, <zvanje>  
član: dr <Ime Prezime>, <zvanje>  
mentor: dr <Ime Prezime>, <zvanje>

Potpis mentora: ____________________

---

## 11. Key words documentation

Accession number, ANO: <enter>  
Identification number, INO: <enter>  
Document type, DT: monographic publication  
Type of record, TR: textual material  
Contents code, CC: bachelor thesis  
Author, AU: <Candidate full name>  
Mentor, MN: <Mentor full name>, <title>, PhD  
Title, TI: Development of a 2D Game Engine with a Scene Editor and Optimized Collision Detection Based on the Segmented Sweep-and-Prune Method  
Language of text, LT: Serbian  
Language of abstract, LA: Serbian / English  
Country of publication, CP: Serbia  
Locality of publication, LP: Vojvodina  
Publication year, PY: 2026  
Publisher, PB: author's reprint  
Publication place, PP: Novi Sad, Faculty of Technical Sciences, Trg Dositeja Obradovića 6  
Physical description, PD: <number of chapters / pages / references / tables / figures / charts / appendices>  
Scientific field, SF: Software Engineering and Information Technologies  
Scientific discipline, SD: Software Systems Engineering / Game Development / Computer Graphics  
Subject / Keywords, S/KW: game engine, collision detection, sweep and prune, scene editor, component-based architecture  
UDC: <enter>  
Holding data, HD: Library of the Faculty of Technical Sciences, Trg Dositeja Obradovića 6, Novi Sad  
Note, N: <enter if needed>  
Abstract, AB:

This thesis presents the development of Platformator, a 2D game engine accompanied by a desktop scene editor and a physics subsystem for collision detection and resolution. The core of the system is based on a component-oriented architecture and implemented in C++20 using SDL3, Eigen, oneTBB and JSON scene serialization. A particular focus of the work is the broad-phase collision detection subsystem based on spatial grid subdivision, AABB structures and segmented interval lists inspired by the method proposed by Tracy, Buss and Woods. In addition to the runtime core, a Unity-like scene editor was developed to support hierarchy browsing, component inspection, asset management and direct scene execution. The system was validated through automated tests and a Mario-like example game. The obtained results indicate that the solution successfully combines a research-inspired collision detection approach with a practical development environment for 2D games, while also exposing clear directions for future improvements.

Accepted by scientific board on, ASB: <enter>  
Defended on, DE: <enter>  
Defense board, DB:  
president: <Full name>, <title>, PhD  
member: <Full name>, <title>, PhD  
mentor: <Full name>, <title>, PhD

Mentor's signature: ____________________

---

## Kako koristiti ovaj primer

1. Zameniti sve tekstove u uglastim ili kosim zagradama stvarnim podacima.
2. Prebaciti sadržaj u zvanični FTN šablon u Word-u ili LaTeX-u, u zavisnosti od zahteva mentora.
3. U poglavlju 5 dopuniti Tabelu 2 stvarnim benchmark merenjima.
4. U literaturi proveriti tačan bibliografski format, redosled autora, naziv konferencije i stil citiranja.
5. Ako mentor želi strogu podelu na zasebna poglavlja „Eksperimenti“ i „Rezultati i diskusija“, poglavlje 5 podeliti na dva dela bez menjanja suštine teksta.