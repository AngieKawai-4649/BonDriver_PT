# PT1 PT2 用BonDriver  

ベース：BonDriver_PT-ST(up0305)  

## 【改造箇所】  
１．設定ファイルフォーマット変更

    BonDriver_PT-S.ChSet.txt  
    BonDriver_PT-T.ChSet.txt  
    のファイルフォーマットを修正  
    ファイルフォーマットはBonDriver_PT-S or T.ChSet.txt  
    にコメントで記載しているので参照  
    なので既存のファイルは使用不可  

２．Visual C++ 2022 にてソリューション作り直し  

    ビルド時のエラー・ワーニング除去  
    細かいバグ修正  
