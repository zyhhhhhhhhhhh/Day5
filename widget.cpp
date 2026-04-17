#include "widget.h"
#include "ui_widget.h"
#include <QDebug>
#include <QPixmap>

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{


    QPixmap pixmap(":/photo/QQ_1776409509879.png");
    QSize labelSize = ui->label->size();
    QPixmap scaledPixmap = pixmap.scaled(labelSize,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    ui->label->setPixmap(scaledPixmap);
}

Widget::~Widget()
{
    delete ui;
}



void Widget::on_pushButton_3_clicked()
{

}

