#include "layermanager.h"

#include <QPixmap>
#include <QPainter>
#include <QDebug>

LayerManager::LayerManager(const QSize &initSize)
    :lastSelected(0),
      layerSize_(initSize)
{
}

LayerPointer LayerManager::layerFrom(int pos) const
{
    if (pos < 0 || pos >= layerLinks.count()) {
        return LayerPointer();
    }
    return layers[layerLinks[pos]];
}

LayerPointer LayerManager::layerFrom(const QString &name) const
{
    if(!exists(name)){
        qDebug()<<"Warnning: try to access a non-existent layer";
        return LayerPointer();
    }
    return layers[name];
}

LayerPointer LayerManager::topLayer() const
{
    return layerLinks.isEmpty() ? LayerPointer() : layers[layerLinks.last()];
}

LayerPointer LayerManager::bottomLayer() const
{
    return layerLinks.isEmpty() ? LayerPointer() : layers[layerLinks.first()];
}

void LayerManager::updateSelected()
{
    for(int i=0;i<layerLinks.count();++i){
        LayerPointer l = layers[layerLinks[i]];
        if(l->isSelected() && l!=lastSelected){
            if (lastSelected) lastSelected->deselect();
            lastSelected = l;
        }
    }
}

LayerPointer LayerManager::selectedLayer() const
{
    return lastSelected;
}

LayerPointer LayerManager::topShownLayer() const
{
    for(int i=layerLinks.count()-1;i>=0;--i){
        if(layers[layerLinks[i]]->isHided()){
            continue;
        }else{
            return layers[layerLinks[i]];
        }
    }
    qWarning()<<"topShownLayer() returns null ptr";
    return LayerPointer();
}

void LayerManager::select(const QString &name)
{
    if(exists(name)){
        LayerPointer l = layers[name];
        if(lastSelected) lastSelected->deselect();
        l->select();
        lastSelected = l;
    }else{
        qDebug()<<"Selected an non-exists layer";
    }
}

void LayerManager::insertLayer(LayerPointer image, const QString &name, int pos)
{
    layerLinks.insert(pos,name);
    layers.insert(name,image);
    qDebug()<<"instert"<<name<<"at"<<pos;
}

void LayerManager::appendLayer(LayerPointer image, const QString &name)
{
    if(layers.contains(name)){
        qDebug()<<"Dupli";
    }
    layerLinks.append(name);
    layers.insert(name,image);
    qDebug()<<"append"<<name<<"at"<<(layerLinks.count()-1);
}

LayerPointer LayerManager::appendLayer(const QString &name)
{
    if(layers.contains(name)){
        qWarning()<<"Duplicated layer skipped!";
        return LayerPointer();
    }
    layerLinks.append(name);
    LayerPointer lp(new Layer(name, layerSize_));
    layers.insert(name, lp);
    qDebug()<<"append"<<name<<"at"<<(layerLinks.count()-1);
    return lp;
}

void LayerManager::removeLayer(const QString &name)
{
    if(!exists(name))return;
    if(layers[name]->isLocked()){
        qDebug()<<"Warning: try to remove locked layer";
        return;
    }
    const bool wasSelected = lastSelected == layers[name];
    layers.remove(name);
    layerLinks.removeAll(name);
    if (wasSelected) {
        lastSelected.clear();
        if (!layerLinks.isEmpty()) select(layerLinks.last());
    }
    qDebug()<<"remove"<<name;
}

void LayerManager::clearLayer(const QString &name)
{
    if(!exists(name)) return;
    layers[name]->clear();
    qDebug()<<"clear content of"<<name;
}

void LayerManager::clearAllLayer()
{
    for(auto &item: layers.values()){
        if (!item->isLocked()) item->clear();
    }
    qDebug()<<"all layers cleared";
}

void LayerManager::reset()
{
    lastSelected.clear();
    layerLinks.clear();
    layers.clear();
}

void LayerManager::moveUp(const QString &name)
{
    const int index = layerLinks.indexOf(name);
    if (index >= 0 && index + 1 < layerLinks.size()) moveTo(index, index + 1);
}

void LayerManager::moveDown(const QString &name)
{
    const int index = layerLinks.indexOf(name);
    if (index > 0) moveTo(index, index - 1);
}

void LayerManager::moveTo(int from, int to)
{
    if (from >= 0 && from < layerLinks.size() && to >= 0 && to < layerLinks.size())
        layerLinks.move(from, to);
}

void LayerManager::moveTo(const QString &name, int to)
{
    moveTo(layerLinks.indexOf(name), to);
}

bool LayerManager::exists(const QString &name) const
{
    return layers.contains(name);
}

bool LayerManager::exists(int pos) const
{
    return pos >= 0 && pos < layerLinks.count();
}

void LayerManager::rename(const QString &oname,const QString &nname)
{
    if (layers.contains(oname) && !layers.contains(nname)) {
        layers[nname] = layers.take(oname);
        layers[nname]->rename(nname);
        int i = layerLinks.indexOf(oname);
        layerLinks[i] = nname;
    }
}

void LayerManager::resizeLayers(const QSize &newsize)
{
    layerSize_ = newsize;
    for(int i=0;i<layerLinks.count();++i){
        layers[layerLinks[i]]->resize(layerSize_);
    }
    qDebug()<<"LayerManager::resizeLayers:"<<layerSize_;
}

void LayerManager::combineLayers(QImage *p, const QRect &rect)
{
    if (p->size() != layerSize_) {
        *p = QImage(layerSize_, QImage::Format_ARGB32_Premultiplied);
        p->fill(Qt::white);
    }
    QPainter painter(p);
    if (!rect.isNull()) painter.setClipRect(rect);
    painter.fillRect(rect.isNull() ? p->rect() : rect, Qt::white);
    int lc = this->count();
    QImage * im = 0;
    for(int i=0;i<lc;++i){
        LayerPointer l = layerFrom(i);
        if( l->isHided() || !l->isTouched() ){
            continue;
        }
        im = l->imagePtr();
        if(rect.isNull()){
            painter.drawImage(0, 0, *im);
        }else{
            painter.drawImage(QRectF(rect), *im, QRectF(rect));
        }
    }
}
